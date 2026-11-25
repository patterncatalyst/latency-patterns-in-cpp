// examples/07-caching/cpp/src/main.cpp
//
// One shared Zipf-skewed barcode trace, run through an uncached baseline and
// four replacement-policy caches (LRU, LFU, FIFO, SIEVE) in front of the
// same slow RouteResolver, then a second trace through two TtlLruCache
// instances (short vs long TTL) across a simulated re-route event, then a
// third, independent trace through a memoized pure dimension classifier
// versus calling it directly. See the chapter prose for why these are one
// chapter's measured lanes rather than four separate examples.
//
// Usage: ch07_caching <results-dir> <scrub-script> [python3]
//   Same contract as every prior chapter's example.
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ch07/cache_trial.hpp"
#include "ch07/dimension_classifier.hpp"
#include "ch07/fifo_cache.hpp"
#include "ch07/lfu_cache.hpp"
#include "ch07/lru_cache.hpp"
#include "ch07/memoizer.hpp"
#include "ch07/route_resolver.hpp"
#include "ch07/sieve_cache.hpp"
#include "ch07/ttl_lru_cache.hpp"
#include "ch07/zipf_trace.hpp"
#include "measure/latency_recorder.hpp"
#include "measure/logging.hpp"
#include "measure/scrub.hpp"

namespace {

namespace fs = std::filesystem;
using measure::Histogram;
using measure::LatencyRecorder;

// This chapter's fixed shape: a 2048-key backing table, a 128-entry cache
// (about 6% of the key space), and a Zipf skew sharp enough that a small hot
// set dominates the traffic (see the chapter prose, "Why the policies need
// a skewed trace" for what a uniform trace would have hidden).
constexpr int key_space = 2048;
constexpr std::size_t cache_capacity = 128;
constexpr double zipf_skew = 1.1;
constexpr std::size_t trace_length = 50'000;
constexpr std::uint64_t trace_seed = 1;

// The TTL trial's own trace, long enough to have a clear before-and-after
// around its reroute point.
constexpr std::size_t ttl_trace_length = 50'000;
constexpr std::size_t reroute_at_index = ttl_trace_length / 2;
constexpr int reroute_key_count = 10;
constexpr auto ttl_tick = std::chrono::microseconds(1);
constexpr std::int64_t short_ttl_ticks = 2'000;     // expires often across the post-reroute window
constexpr std::int64_t long_ttl_ticks = 1'000'000;  // longer than this trial's whole span: effectively never

// The memoization trial's own trace: a small, fixed set of standard parcel
// dimension tuples, Zipf-skewed the same way the barcode trace is, so
// repeats are common enough for memoization to matter.
constexpr int dimension_key_space = 24;
constexpr double dimension_skew = 1.1;
constexpr std::size_t dimension_trace_length = 50'000;
constexpr std::uint64_t dimension_seed = 7;

std::string read_profile_header(std::istream& in) {
    std::ostringstream header;
    std::string line;
    while (std::getline(in, line)) {
        header << line << '\n';
    }
    return header.str();
}

// Same ASCII histogram renderer every prior chapter's example uses (see
// ch03's main.cpp for why this stays chapter-local rather than moving into
// examples/_lib).
std::string render_histogram(const Histogram& hist) {
    std::ostringstream out;
    const auto max_count = *std::ranges::max_element(hist.counts);
    constexpr int bar_width = 40;
    for (std::size_t i = 0; i < hist.counts.size(); ++i) {
        const double bucket_start_us =
            (hist.min_value + (static_cast<double>(i) * hist.bucket_width)) * 1e6;
        const double bucket_end_us = bucket_start_us + (hist.bucket_width * 1e6);
        const auto filled =
            max_count == 0 ? 0 : static_cast<int>(hist.counts[i] * bar_width / max_count);
        out << std::fixed << std::setprecision(3) << std::setw(10) << bucket_start_us << " us - "
            << std::setw(10) << bucket_end_us << " us | " << std::string(static_cast<std::size_t>(filled), '#')
            << ' ' << hist.counts[i] << '\n';
    }
    return out.str();
}

LatencyRecorder recorder_from(const std::vector<ch07::Sample>& samples) {
    LatencyRecorder recorder;
    for (const ch07::Sample& sample : samples) {
        recorder.record(sample);
    }
    return recorder;
}

void write_lane(std::ostream& out, const std::string& key_prefix, const LatencyRecorder& recorder) {
    out << key_prefix << "_sample_count=" << recorder.sample_count() << '\n';
    out << key_prefix << "_p50_us=" << std::fixed << std::setprecision(3)
        << recorder.percentile(50) * 1e6 << '\n';
    out << key_prefix << "_p99_us=" << recorder.percentile(99) * 1e6 << '\n';
    out << key_prefix << "_p999_us=" << recorder.percentile(99.9) * 1e6 << '\n';
    out << "# " << key_prefix << " histogram (bucket range in microseconds, count, bar)\n";
    out << render_histogram(recorder.histogram());
}

struct LaneSummary {
    std::string name;
    double hit_ratio;
    double p50_us;
    double p99_us;
    double p999_us;
};

void log_lane(const LaneSummary& lane) {
    spdlog::info("{}: hit_ratio={:.3f} p50={:.3f}us p99={:.3f}us p99.9={:.3f}us", lane.name, lane.hit_ratio,
                 lane.p50_us, lane.p99_us, lane.p999_us);
}

LaneSummary make_lane_summary(std::string name, std::size_t hit_count, std::size_t miss_count,
                               const LatencyRecorder& recorder) {
    return LaneSummary{
        .name = std::move(name),
        .hit_ratio = ch07::hit_ratio(hit_count, miss_count),
        .p50_us = recorder.percentile(50) * 1e6,
        .p99_us = recorder.percentile(99) * 1e6,
        .p999_us = recorder.percentile(99.9) * 1e6,
    };
}

}  // namespace

int main(int argc, char** argv) {
    measure::init_logging("ch07-caching");

    if (argc < 3) {
        spdlog::error("usage: ch07_caching <results-dir> <scrub-script> [python3]");
        return EXIT_FAILURE;
    }
    const fs::path results_dir = argv[1];
    const fs::path scrub_script = argv[2];
    const fs::path python3 = argc > 3 ? fs::path(argv[3]) : fs::path("python3");

    const std::string profile_header = read_profile_header(std::cin);

    fs::create_directories(results_dir);
    measure::ResultsDirScrubGuard scrub_guard(results_dir, scrub_script, python3);

    std::ofstream out(results_dir / "caching.txt", std::ios::trunc);
    out << "# Chapter 7 caching run: one Zipf-skewed barcode-to-route access\n";
    out << "# trace run uncached, then through LRU/LFU/FIFO/SIEVE caches in\n";
    out << "# front of the SAME slow RouteResolver (see ch07::RouteResolver);\n";
    out << "# a second trace through TtlLruCache at two TTL windows across a\n";
    out << "# simulated reroute; a third trace through a memoized pure\n";
    out << "# dimension classifier versus calling it directly.\n";
    out << "# mismatch_count/stale_serve_count are this chapter's behavioral\n";
    out << "# checks: a plain eviction-policy lane must read 0 mismatches\n";
    out << "# (the backing table never changes during that lane's run), and\n";
    out << "# a TTL lane's stale_serve_count is the staleness this chapter is\n";
    out << "# measuring, not a bug.\n";
    out << profile_header;
    out << "key_space=" << key_space << '\n';
    out << "cache_capacity=" << cache_capacity << '\n';
    out << "zipf_skew=" << zipf_skew << '\n';
    out << "trace_length=" << trace_length << '\n';

    // --- Part 1: uncached baseline + the four replacement policies --------
    const ch07::RouteResolver resolver(key_space);
    const ch07::ZipfTrace trace_gen(key_space, zipf_skew, trace_seed);
    const std::vector<int> trace = trace_gen.generate(trace_length);

    std::vector<LaneSummary> summaries;

    spdlog::info("running the uncached baseline over {} accesses...", trace.size());
    const ch07::CacheTrialResult uncached = ch07::run_uncached_trial(resolver, trace);
    const LatencyRecorder uncached_recorder = recorder_from(uncached.samples);
    out << "uncached_hit_count=" << uncached.hit_count << '\n';
    out << "uncached_miss_count=" << uncached.miss_count << '\n';
    out << "uncached_hit_ratio=" << std::fixed << std::setprecision(4)
        << ch07::hit_ratio(uncached.hit_count, uncached.miss_count) << '\n';
    out << "uncached_mismatch_count=" << uncached.mismatch_count << '\n';
    write_lane(out, "uncached", uncached_recorder);
    summaries.push_back(make_lane_summary("uncached", uncached.hit_count, uncached.miss_count, uncached_recorder));

    auto run_policy_lane = [&](const std::string& name, auto&& cache) {
        spdlog::info("running the {} lane over {} accesses...", name, trace.size());
        const ch07::CacheTrialResult result = ch07::run_cache_trial(cache, resolver, trace);
        const LatencyRecorder recorder = recorder_from(result.samples);
        out << name << "_hit_count=" << result.hit_count << '\n';
        out << name << "_miss_count=" << result.miss_count << '\n';
        out << name << "_hit_ratio=" << std::fixed << std::setprecision(4)
            << ch07::hit_ratio(result.hit_count, result.miss_count) << '\n';
        out << name << "_mismatch_count=" << result.mismatch_count << '\n';
        write_lane(out, name, recorder);
        summaries.push_back(make_lane_summary(name, result.hit_count, result.miss_count, recorder));
    };

    ch07::LruCache lru(cache_capacity);
    run_policy_lane("lru", lru);
    ch07::LfuCache lfu(cache_capacity);
    run_policy_lane("lfu", lfu);
    ch07::FifoCache fifo(cache_capacity);
    run_policy_lane("fifo", fifo);
    ch07::SieveCache sieve(cache_capacity);
    run_policy_lane("sieve", sieve);

    // --- Part 2: TTL across a simulated reroute ----------------------------
    const ch07::ZipfTrace ttl_trace_gen(key_space, zipf_skew, trace_seed + 1);
    const std::vector<int> ttl_trace = ttl_trace_gen.generate(ttl_trace_length);
    const std::vector<int> reroute_keys = ttl_trace_gen.hottest_keys(reroute_key_count);
    out << "reroute_at_index=" << reroute_at_index << '\n';
    out << "reroute_key_count=" << reroute_key_count << '\n';

    auto run_ttl_lane = [&](const std::string& name, std::int64_t ttl_ticks) {
        ch07::RouteResolver ttl_resolver(key_space);
        ch07::TtlLruCache cache(cache_capacity, ttl_ticks * ttl_tick);
        spdlog::info("running the {} lane over {} accesses (reroute at {})...", name, ttl_trace.size(),
                     reroute_at_index);
        const ch07::StalenessTrialResult result =
            ch07::run_ttl_trial(cache, ttl_resolver, ttl_trace, reroute_keys, reroute_at_index, ttl_tick);
        const LatencyRecorder recorder = recorder_from(result.samples);
        out << name << "_ttl_ticks=" << ttl_ticks << '\n';
        out << name << "_hit_count=" << result.hit_count << '\n';
        out << name << "_miss_count=" << result.miss_count << '\n';
        out << name << "_hit_ratio=" << std::fixed << std::setprecision(4)
            << ch07::hit_ratio(result.hit_count, result.miss_count) << '\n';
        out << name << "_stale_serve_count=" << result.stale_serve_count << '\n';
        write_lane(out, name, recorder);
        summaries.push_back(make_lane_summary(name, result.hit_count, result.miss_count, recorder));
        spdlog::info("{}: stale_serve_count={}", name, result.stale_serve_count);
    };

    run_ttl_lane("ttl_short", short_ttl_ticks);
    run_ttl_lane("ttl_long", long_ttl_ticks);

    // --- Part 3: memoizing a pure function ---------------------------------
    std::vector<std::array<double, 3>> dimension_table;
    dimension_table.reserve(dimension_key_space);
    for (int i = 0; i < dimension_key_space; ++i) {
        dimension_table.push_back({
            10.0 + (static_cast<double>(i) * 7.0),
            8.0 + (static_cast<double>(i) * 5.0),
            5.0 + (static_cast<double>(i) * 4.0),
        });
    }
    auto classify_from_index = [&dimension_table](int index) {
        const auto& dims = dimension_table[static_cast<std::size_t>(index)];
        return ch07::classify_dimensions(dims[0], dims[1], dims[2]);
    };

    const ch07::ZipfTrace dimension_trace_gen(dimension_key_space, dimension_skew, dimension_seed);
    const std::vector<int> dimension_trace = dimension_trace_gen.generate(dimension_trace_length);

    spdlog::info("running the direct (unmemoized) dimension classifier over {} calls...", dimension_trace.size());
    std::vector<ch07::Sample> direct_samples;
    direct_samples.reserve(dimension_trace.size());
    std::size_t memo_mismatches = 0;
    for (const int key : dimension_trace) {
        const auto started_at = std::chrono::steady_clock::now();
        const ch07::SizeClass value = classify_from_index(key);
        const auto finished_at = std::chrono::steady_clock::now();
        direct_samples.emplace_back(finished_at - started_at);
        (void)value;
    }
    const LatencyRecorder direct_recorder = recorder_from(direct_samples);
    write_lane(out, "dimension_direct", direct_recorder);

    spdlog::info("running the memoized dimension classifier over {} calls...", dimension_trace.size());
    ch07::Memoizer<int, ch07::SizeClass, decltype(classify_from_index)> memoized(classify_from_index);
    std::vector<ch07::Sample> memo_samples;
    memo_samples.reserve(dimension_trace.size());
    for (const int key : dimension_trace) {
        const auto started_at = std::chrono::steady_clock::now();
        const ch07::SizeClass value = memoized(key);
        const auto finished_at = std::chrono::steady_clock::now();
        memo_samples.emplace_back(finished_at - started_at);
        if (value != classify_from_index(key)) {
            ++memo_mismatches;
        }
    }
    const LatencyRecorder memo_recorder = recorder_from(memo_samples);
    out << "dimension_memo_hit_count=" << memoized.hit_count() << '\n';
    out << "dimension_memo_miss_count=" << memoized.miss_count() << '\n';
    out << "dimension_memo_hit_ratio=" << std::fixed << std::setprecision(4)
        << ch07::hit_ratio(memoized.hit_count(), memoized.miss_count()) << '\n';
    out << "dimension_memo_mismatch_count=" << memo_mismatches << '\n';
    write_lane(out, "dimension_memo", memo_recorder);

    spdlog::info("dimension_direct: p50={:.3f}us p99={:.3f}us p99.9={:.3f}us", direct_recorder.percentile(50) * 1e6,
                 direct_recorder.percentile(99) * 1e6, direct_recorder.percentile(99.9) * 1e6);
    spdlog::info("dimension_memo:   p50={:.3f}us p99={:.3f}us p99.9={:.3f}us (hit_ratio={:.3f}, mismatches={})",
                 memo_recorder.percentile(50) * 1e6, memo_recorder.percentile(99) * 1e6,
                 memo_recorder.percentile(99.9) * 1e6, ch07::hit_ratio(memoized.hit_count(), memoized.miss_count()),
                 memo_mismatches);

    for (const LaneSummary& lane : summaries) {
        log_lane(lane);
    }
    spdlog::info("results written to {}", (results_dir / "caching.txt").string());

    return EXIT_SUCCESS;
}
