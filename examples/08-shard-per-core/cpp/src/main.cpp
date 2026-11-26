// examples/08-shard-per-core/cpp/src/main.cpp
//
// Chapter 8: the in-flight parcel tracker, keyed by barcode, measured two
// ways: one map behind one shared lock (every core contends, regardless of
// which barcode it saw), and shard-per-core (one worker owns one shard, no
// lock on the hot path, routed by direct hash). A second pair of trials
// induces hot-partition skew (a handful of barcodes that collide onto the
// same shard under direct hashing), then rebalances one of those barcodes
// onto an otherwise idle shard via the routing-table indirection and
// re-measures (see CLAUDE.md, "The spine"; _plans/premise-probe.md for the
// measured effect).
//
// Usage: ch08_shard_per_core <results-dir> <scrub-script> [python3]
//   Same contract as every prior chapter's example.
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "ch08/event_generator.hpp"
#include "ch08/partition_trial.hpp"
#include "ch08/routing.hpp"
#include "ch08/shared_lock_tracker.hpp"
#include "ch08/sharded_tracker_pool.hpp"
#include "measure/latency_recorder.hpp"
#include "measure/logging.hpp"
#include "measure/scrub.hpp"

namespace {

namespace fs = std::filesystem;
using measure::Histogram;
using measure::LatencyRecorder;

// Lane 1 vs lane 2: a uniform-keyspace batch large enough that one shared
// mutex's contention cost is visible against 22 cores all serializing
// through it.
constexpr std::uint64_t contention_item_count = 400'000;
constexpr std::uint64_t contention_key_space = 50'000;

// Lanes 3 and 4: a smaller, deliberately skewed batch.
constexpr std::uint64_t skew_item_count = 200'000;
constexpr double hot_fraction = 0.60;
constexpr std::uint64_t skew_cold_key_space = 20'000;
constexpr std::size_t hot_key_count = 2;

std::string read_profile_header(std::istream& in) {
    std::ostringstream header;
    std::string line;
    while (std::getline(in, line)) {
        header << line << '\n';
    }
    return header.str();
}

// Same ASCII histogram renderer every prior chapter's example uses (see
// ch02's main.cpp for why this stays chapter-local rather than moving into
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

LatencyRecorder recorder_from_us(const std::vector<double>& completion_us) {
    LatencyRecorder recorder;
    for (const double us : completion_us) {
        recorder.record(std::chrono::duration<double>(us * 1e-6));
    }
    return recorder;
}

void write_recorder(std::ostream& out, const std::string& key_prefix, const LatencyRecorder& recorder) {
    out << key_prefix << "_sample_count=" << recorder.sample_count() << '\n';
    if (recorder.sample_count() == 0) {
        return;
    }
    out << key_prefix << "_p50_us=" << std::fixed << std::setprecision(3) << recorder.percentile(50) * 1e6
        << '\n';
    out << key_prefix << "_p99_us=" << recorder.percentile(99) * 1e6 << '\n';
    out << key_prefix << "_p999_us=" << recorder.percentile(99.9) * 1e6 << '\n';
}

void write_contention_lane(std::ostream& out, const std::string& key_prefix,
                            const ch08::PartitionTrialResult& trial) {
    const LatencyRecorder recorder = recorder_from_us(trial.completion_us);
    const auto item_count = static_cast<std::uint64_t>(trial.completion_us.size());
    const double throughput_items_per_sec =
        trial.total_wall_time_us > 0.0 ? static_cast<double>(item_count) / (trial.total_wall_time_us * 1e-6)
                                        : 0.0;

    out << key_prefix << "_item_count=" << item_count << '\n';
    out << key_prefix << "_wall_time_us=" << std::fixed << std::setprecision(3) << trial.total_wall_time_us
        << '\n';
    out << key_prefix << "_throughput_items_per_sec=" << std::setprecision(1) << throughput_items_per_sec
        << '\n';
    write_recorder(out, key_prefix, recorder);
    out << "# " << key_prefix
        << " completion-time histogram (bucket range in microseconds since batch start, count, bar)\n";
    out << render_histogram(recorder.histogram());
}

// seen_count mismatches between the shared-lock tracker and the sharded
// pool's per-shard maps, across every distinct key the batch touched. Both
// trackers ran over the identical event sequence, so a correct shard-per-
// core implementation must agree with the shared-lock baseline on every
// key's seen_count.
std::uint64_t count_state_mismatches(const std::vector<ch08::Barcode>& events,
                                      const ch08::SharedLockTracker& shared_tracker,
                                      const ch08::ShardedTrackerPool& pool) {
    std::unordered_set<ch08::Barcode> distinct_keys(events.begin(), events.end());
    std::uint64_t mismatches = 0;
    for (const ch08::Barcode key : distinct_keys) {
        const ch08::TrackedState shared_state = shared_tracker.snapshot(key);
        const std::size_t shard = ch08::hash_route(key, pool.shard_count());
        const ch08::TrackedState sharded_state = pool.snapshot(key, shard);
        if (shared_state.seen_count != sharded_state.seen_count) {
            ++mismatches;
        }
    }
    return mismatches;
}

struct SkewLaneReport {
    std::vector<std::uint64_t> shard_loads;
    LatencyRecorder hot_shard_recorder;
    LatencyRecorder other_shards_recorder;
};

SkewLaneReport build_skew_report(const std::vector<ch08::Barcode>& events,
                                  const ch08::ShardedTrackerPool& pool,
                                  const std::function<std::size_t(ch08::Barcode)>& router,
                                  const ch08::PartitionTrialResult& trial, std::size_t hot_shard) {
    SkewLaneReport report;
    report.shard_loads.resize(pool.shard_count());
    for (std::size_t s = 0; s < pool.shard_count(); ++s) {
        report.shard_loads[s] = pool.shard_processed_count(s);
    }
    for (std::size_t i = 0; i < events.size(); ++i) {
        const std::size_t shard = router(events[i]);
        const double us = trial.completion_us[i];
        if (shard == hot_shard) {
            report.hot_shard_recorder.record(std::chrono::duration<double>(us * 1e-6));
        } else {
            report.other_shards_recorder.record(std::chrono::duration<double>(us * 1e-6));
        }
    }
    return report;
}

void write_skew_lane(std::ostream& out, const std::string& key_prefix, const SkewLaneReport& report,
                      std::size_t hot_shard, std::uint64_t total_item_count) {
    const std::uint64_t hot_load = report.shard_loads.at(hot_shard);
    std::uint64_t max_other_load = 0;
    for (std::size_t s = 0; s < report.shard_loads.size(); ++s) {
        if (s != hot_shard) {
            max_other_load = std::max(max_other_load, report.shard_loads[s]);
        }
    }

    out << key_prefix << "_hot_shard_index=" << hot_shard << '\n';
    out << key_prefix << "_hot_shard_processed_count=" << hot_load << '\n';
    out << key_prefix << "_hot_shard_load_percent=" << std::fixed << std::setprecision(2)
        << (100.0 * static_cast<double>(hot_load) / static_cast<double>(total_item_count)) << '\n';
    out << key_prefix << "_max_other_shard_processed_count=" << max_other_load << '\n';
    out << key_prefix << "_hot_vs_max_other_ratio=" << std::setprecision(2)
        << (max_other_load == 0 ? 0.0 : static_cast<double>(hot_load) / static_cast<double>(max_other_load))
        << '\n';
    write_recorder(out, key_prefix + "_hot_shard", report.hot_shard_recorder);
    write_recorder(out, key_prefix + "_other_shards", report.other_shards_recorder);
}

}  // namespace

int main(int argc, char** argv) {
    measure::init_logging("ch08-shard-per-core");

    if (argc < 3) {
        spdlog::error("usage: ch08_shard_per_core <results-dir> <scrub-script> [python3]");
        return EXIT_FAILURE;
    }
    const fs::path results_dir = argv[1];
    const fs::path scrub_script = argv[2];
    const fs::path python3 = argc > 3 ? fs::path(argv[3]) : fs::path("python3");

    const std::string profile_header = read_profile_header(std::cin);

    fs::create_directories(results_dir);
    measure::ResultsDirScrubGuard scrub_guard(results_dir, scrub_script, python3);

    const unsigned hw_concurrency = std::thread::hardware_concurrency();
    const std::size_t shard_count = hw_concurrency > 0 ? hw_concurrency : 4;

    std::ofstream out(results_dir / "shard-per-core.txt", std::ios::trunc);
    out << "# Chapter 8 shard-per-core run: an in-flight parcel tracker keyed\n";
    out << "# by barcode, measured as one shared map behind one lock versus\n";
    out << "# shard-per-core (direct hash routing, no lock on the hot path),\n";
    out << "# then the same sharded design under induced hot-key skew before\n";
    out << "# and after rebalancing one colliding hot key onto an idle shard.\n";
    out << profile_header;
    out << "shard_count=" << shard_count << '\n';

    // --- Lanes 1 and 2: shared-lock versus shard-per-core, uniform keys ---
    spdlog::info("generating {} uniform-keyspace events ({} distinct keys)...", contention_item_count,
                 contention_key_space);
    const std::vector<ch08::Barcode> uniform_events =
        ch08::generate_uniform_events(contention_item_count, contention_key_space, 0xC0FFEEULL);

    spdlog::info("shared-lock lane: {} events across {} worker threads, one tracker, one mutex...",
                 contention_item_count, shard_count);
    ch08::SharedLockTracker shared_tracker;
    const ch08::PartitionTrialResult shared_lock_result =
        ch08::run_shared_lock_trial(uniform_events, shared_tracker, shard_count);

    spdlog::info("sharded lane: {} events across {} shard-owning workers, direct hash routing...",
                 contention_item_count, shard_count);
    ch08::ShardedTrackerPool contention_pool(shard_count, /*pin_workers_to_cores=*/true);
    const auto direct_router = [&](ch08::Barcode key) { return ch08::hash_route(key, contention_pool.shard_count()); };
    const ch08::PartitionTrialResult sharded_result =
        ch08::run_sharded_trial(uniform_events, contention_pool, direct_router);

    const std::uint64_t state_mismatches = count_state_mismatches(uniform_events, shared_tracker, contention_pool);
    spdlog::info("shared-lock vs sharded state mismatches: {}", state_mismatches);

    out << "\n# --- lane 1: shared-lock (one map, one mutex, " << shard_count << " contending threads) ---\n";
    write_contention_lane(out, "shared_lock", shared_lock_result);

    out << "\n# --- lane 2: shard-per-core (" << shard_count << " shards, direct hash routing, no lock) ---\n";
    write_contention_lane(out, "sharded", sharded_result);
    out << "sharded_vs_shared_lock_state_mismatch_count=" << state_mismatches << '\n';

    // --- Lanes 3 and 4: induced hot-partition skew, before and after ------
    spdlog::info("searching for {} barcodes that collide onto the same shard under direct hash routing...",
                 hot_key_count);
    const std::vector<ch08::Barcode> hot_keys = ch08::find_colliding_hot_keys(shard_count, hot_key_count);
    if (hot_keys.size() < hot_key_count) {
        spdlog::error("could not find {} colliding hot keys for shard_count={}", hot_key_count, shard_count);
        return EXIT_FAILURE;
    }
    const std::size_t hot_shard = ch08::hash_route(hot_keys.front(), shard_count);
    spdlog::info("hot keys {} and {} both route to shard {}", hot_keys[0], hot_keys[1], hot_shard);

    const std::vector<ch08::Barcode> skewed_events =
        ch08::generate_skewed_events(skew_item_count, hot_keys, hot_fraction, skew_cold_key_space, 0x5EED5EEDULL);

    spdlog::info("skew lane (before mitigation): {} events, direct hash routing...", skew_item_count);
    ch08::ShardedTrackerPool skew_pool_before(shard_count, /*pin_workers_to_cores=*/true);
    const auto skew_router_before = [&](ch08::Barcode key) {
        return ch08::hash_route(key, skew_pool_before.shard_count());
    };
    const ch08::PartitionTrialResult skew_result_before =
        ch08::run_sharded_trial(skewed_events, skew_pool_before, skew_router_before);
    const SkewLaneReport report_before =
        build_skew_report(skewed_events, skew_pool_before, skew_router_before, skew_result_before, hot_shard);

    // Mitigation: a routing table indirection moves hot_keys[1] off the
    // shard it shares with hot_keys[0] and onto an otherwise cold shard;
    // hot_keys[0] keeps the hot shard. The table needs more buckets than
    // shards for this to isolate one key: with bucket_count == shard_count,
    // bucket_for_key collapses to the exact same formula as hash_route, so
    // two keys that collide on a shard also collide on a bucket and cannot
    // be moved independently. routing_table_fanout buckets per shard keeps
    // the table's default assignment identical to hash_route (bucket_count
    // is an exact multiple of shard_count, so bucket % shard_count
    // reproduces it) while making two specific keys sharing one bucket, as
    // opposed to one shard, astronomically unlikely.
    constexpr std::size_t routing_table_fanout = 1024;
    const std::size_t target_shard = (hot_shard + 1) % shard_count;
    ch08::RoutingTable rebalanced_table(shard_count * routing_table_fanout, shard_count);
    const std::size_t moved_bucket = rebalanced_table.bucket_for_key(hot_keys[1]);
    if (moved_bucket == rebalanced_table.bucket_for_key(hot_keys[0])) {
        spdlog::error("hot keys {} and {} share a bucket even at {}x fanout; cannot isolate the move",
                      hot_keys[0], hot_keys[1], routing_table_fanout);
        return EXIT_FAILURE;
    }
    rebalanced_table.assign(moved_bucket, target_shard);
    spdlog::info("mitigation: moving hot key {} from shard {} to shard {} via the routing table", hot_keys[1],
                 hot_shard, target_shard);

    spdlog::info("skew lane (after mitigation): {} events, routing-table indirection...", skew_item_count);
    ch08::ShardedTrackerPool skew_pool_after(shard_count, /*pin_workers_to_cores=*/true);
    const auto skew_router_after = [&](ch08::Barcode key) { return rebalanced_table.route(key); };
    const ch08::PartitionTrialResult skew_result_after =
        ch08::run_sharded_trial(skewed_events, skew_pool_after, skew_router_after);
    const SkewLaneReport report_after =
        build_skew_report(skewed_events, skew_pool_after, skew_router_after, skew_result_after, hot_shard);
    const std::uint64_t target_shard_load_after = report_after.shard_loads.at(target_shard);

    out << "\n# --- lanes 3/4: induced hot-partition skew (" << hot_key_count << " colliding hot keys, "
        << std::fixed << std::setprecision(0) << (hot_fraction * 100.0) << "% of traffic) ---\n";
    out << "skew_item_count=" << skew_item_count << '\n';
    out << "skew_hot_key_count=" << hot_key_count << '\n';
    out << "skew_hot_fraction_percent=" << std::setprecision(2) << (hot_fraction * 100.0) << '\n';
    out << "skew_target_shard_index=" << target_shard << '\n';

    out << "\n# before mitigation: both hot keys route to skew_before_hot_shard_index\n";
    write_skew_lane(out, "skew_before", report_before, hot_shard, skew_item_count);

    out << "\n# after mitigation: hot_keys[1] moved to skew_target_shard_index\n";
    write_skew_lane(out, "skew_after", report_after, hot_shard, skew_item_count);
    out << "skew_after_target_shard_processed_count=" << target_shard_load_after << '\n';

    spdlog::info("shared-lock:  wall={:.3f}ms throughput={:.0f}/s", shared_lock_result.total_wall_time_us / 1000.0,
                 static_cast<double>(contention_item_count) / (shared_lock_result.total_wall_time_us * 1e-6));
    spdlog::info("sharded:      wall={:.3f}ms throughput={:.0f}/s", sharded_result.total_wall_time_us / 1000.0,
                 static_cast<double>(contention_item_count) / (sharded_result.total_wall_time_us * 1e-6));
    spdlog::info("skew before:  hot shard {} processed {} of {} events", hot_shard,
                 report_before.shard_loads.at(hot_shard), skew_item_count);
    spdlog::info("skew after:   hot shard {} processed {} of {} events, shard {} now processed {}", hot_shard,
                 report_after.shard_loads.at(hot_shard), skew_item_count, target_shard, target_shard_load_after);
    spdlog::info("results written to {}", (results_dir / "shard-per-core.txt").string());

    return EXIT_SUCCESS;
}
