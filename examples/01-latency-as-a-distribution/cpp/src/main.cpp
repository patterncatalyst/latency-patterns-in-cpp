// examples/01-latency-as-a-distribution/cpp/src/main.cpp
//
// Drives the shared line-simulator once, under a real per-parcel scan
// workload plus a periodic stall, and records scan-to-divert twice from the
// SAME run: once naively (no coordinated-omission correction) and once
// corrected (see ch01::build_naive_and_corrected). Writes both, plus the
// naive arithmetic mean this chapter critiques, to a profile-labelled
// results file.
//
// Usage: ch01_latency_as_a_distribution <results-dir> <scrub-script> [python3]
//   Reads scripts/profile.sh's KEY=value lines from stdin, same contract as
//   Chapter 0's example (see scripts/profile.sh).
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "ch01/distribution_stats.hpp"
#include "ch01/loaded_scan_stage.hpp"
#include "ch01/sample_collecting_probe.hpp"
#include "line_simulator/line_simulator.hpp"
#include "measure/latency_recorder.hpp"
#include "measure/logging.hpp"
#include "measure/scrub.hpp"

namespace {

namespace fs = std::filesystem;
using line_simulator::LineConfig;
using line_simulator::LineSimulator;
using line_simulator::Stage;
using measure::Histogram;
using measure::LatencyRecorder;

std::string read_profile_header(std::istream& in) {
    std::ostringstream header;
    std::string line;
    while (std::getline(in, line)) {
        header << line << '\n';
    }
    return header.str();
}

// Same ASCII histogram renderer as Chapter 0's example: one row per bucket,
// a bar scaled to the busiest bucket. Kept chapter-local rather than moved
// into examples/_lib, the same call Chapter 0 made: it is a results-writing
// convenience with two call sites so far, not shared measurement logic (see
// CLAUDE.md, "Scope discipline" on keeping the harness capped).
std::string render_histogram(const Histogram& hist) {
    std::ostringstream out;
    const auto max_count = *std::ranges::max_element(hist.counts);
    constexpr int bar_width = 40;
    for (std::size_t i = 0; i < hist.counts.size(); ++i) {
        const double bucket_start_us =
            (hist.min_value + (static_cast<double>(i) * hist.bucket_width)) * 1e6;
        const double bucket_end_us = bucket_start_us + (hist.bucket_width * 1e6);
        const auto filled = max_count == 0
                                 ? 0
                                 : static_cast<int>(hist.counts[i] * bar_width / max_count);
        out << std::fixed << std::setprecision(3) << std::setw(10) << bucket_start_us << " us - "
            << std::setw(10) << bucket_end_us << " us | " << std::string(static_cast<std::size_t>(filled), '#')
            << ' ' << hist.counts[i] << '\n';
    }
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    measure::init_logging("ch01-latency-as-a-distribution");

    if (argc < 3) {
        spdlog::error("usage: ch01_latency_as_a_distribution <results-dir> <scrub-script> [python3]");
        return EXIT_FAILURE;
    }
    const fs::path results_dir = argv[1];
    const fs::path scrub_script = argv[2];
    const fs::path python3 = argc > 3 ? fs::path(argv[3]) : fs::path("python3");

    const std::string profile_header = read_profile_header(std::cin);

    fs::create_directories(results_dir);
    measure::ResultsDirScrubGuard scrub_guard(results_dir, scrub_script, python3);

    // Same belt geometry as Chapter 0 (the budget is physical, see CLAUDE.md,
    // "The spine"); what changes here is the workload and the arrival rate,
    // not the line. parcels_per_second=2000 means the belt is scheduled to
    // demand a parcel every 500us, well inside the stall duration below: the
    // line is configured to run at a rate the scan stage cannot always keep
    // up with, which is what makes the coordinated-omission correction have
    // something real to correct.
    LineConfig config{};
    config.parcel_count = 5000;
    config.parcels_per_second = 2000.0;

    LineSimulator sim{config};

    // Every 250th parcel (0.4% of the run) pays an 8 millisecond stall; the
    // other 99.6% only pay the decode cost. See LoadedScanStage for why this
    // is modeled as a stage override rather than a probe plug-in.
    const ch01::StallSchedule stall{.every_n_parcels = 250, .duration = std::chrono::milliseconds(8)};

    auto probe = std::make_shared<ch01::SampleCollectingProbe>();
    const ch01::LoadedScanStage workload{probe, stall};
    sim.set_stage_work(Stage::Scan, workload);

    spdlog::info("running {} parcels at {} parcels/s (nominal), stall every {} parcels...",
                 config.parcel_count, config.parcels_per_second, stall.every_n_parcels);
    const std::uint64_t completed = sim.run();
    const std::vector<ch01::Sample>& raw = probe->samples();
    spdlog::info("completed {} parcels, {} raw scan-to-divert samples recorded", completed,
                 raw.size());

    const ch01::Sample naive_mean = ch01::compute_mean(raw);
    const ch01::Sample expected_interval{1.0 / config.parcels_per_second};
    const ch01::NaiveAndCorrected dist = ch01::build_naive_and_corrected(raw, expected_interval);

    const double deadline_us = sim.scan_to_divert_deadline().count() * 1e6;

    const double naive_mean_us = naive_mean.count() * 1e6;
    const double naive_p50_us = dist.naive.percentile(50) * 1e6;
    const double naive_p99_us = dist.naive.percentile(99) * 1e6;
    const double naive_p999_us = dist.naive.percentile(99.9) * 1e6;
    const Histogram naive_hist = dist.naive.histogram();

    const double corrected_p50_us = dist.corrected.percentile(50) * 1e6;
    const double corrected_p99_us = dist.corrected.percentile(99) * 1e6;
    const double corrected_p999_us = dist.corrected.percentile(99.9) * 1e6;
    const Histogram corrected_hist = dist.corrected.histogram();

    std::ofstream out(results_dir / "loaded-distribution.txt", std::ios::trunc);
    out << "# Chapter 1 loaded-distribution run: scan-to-divert under a real\n";
    out << "# per-parcel decode workload plus a periodic stall. naive = no\n";
    out << "# coordinated-omission correction; corrected = Gil Tene's backfill\n";
    out << "# turned on. Both are computed by replaying the same raw sample\n";
    out << "# sequence into two recorders (see ch01::build_naive_and_corrected).\n";
    out << profile_header;
    out << "belt_speed_m_per_s=" << config.belt_speed_m_per_s << '\n';
    out << "scan_to_diverter_distance_m=" << config.scan_to_diverter_distance_m << '\n';
    out << "parcels_per_second=" << config.parcels_per_second << '\n';
    out << "parcel_count=" << config.parcel_count << '\n';
    out << "stall_every_n_parcels=" << stall.every_n_parcels << '\n';
    out << "stall_duration_ms=" << std::chrono::duration<double, std::milli>(stall.duration).count()
        << '\n';
    out << "scan_to_divert_deadline_us=" << std::fixed << std::setprecision(3) << deadline_us << '\n';
    out << "raw_sample_count=" << raw.size() << '\n';
    out << "# naive: the measurement a generator that waits for each parcel to\n";
    out << "# finish before starting the next would report. mean_us is the\n";
    out << "# naive arithmetic mean, included ONLY to show it next to the\n";
    out << "# percentiles computed from the same samples; it is not a reported\n";
    out << "# scan-to-divert result anywhere else in this guide.\n";
    out << "naive_sample_count=" << dist.naive.sample_count() << '\n';
    out << "naive_mean_us=" << std::fixed << std::setprecision(3) << naive_mean_us << '\n';
    out << "naive_p50_us=" << naive_p50_us << '\n';
    out << "naive_p99_us=" << naive_p99_us << '\n';
    out << "naive_p999_us=" << naive_p999_us << '\n';
    out << "# corrected: the same raw samples, replayed with the belt's own\n";
    out << "# arrival interval (1 / parcels_per_second) so a stall backfills\n";
    out << "# the arrivals it implies were waiting behind it.\n";
    out << "corrected_sample_count=" << dist.corrected.sample_count() << '\n';
    out << "corrected_p50_us=" << corrected_p50_us << '\n';
    out << "corrected_p99_us=" << corrected_p99_us << '\n';
    out << "corrected_p999_us=" << corrected_p999_us << '\n';
    out << "# naive histogram (bucket range in microseconds, count, bar)\n";
    out << render_histogram(naive_hist);
    out << "# corrected histogram (bucket range in microseconds, count, bar)\n";
    out << render_histogram(corrected_hist);

    spdlog::info("naive:     mean={:.3f}us p50={:.3f}us p99={:.3f}us p99.9={:.3f}us", naive_mean_us,
                 naive_p50_us, naive_p99_us, naive_p999_us);
    spdlog::info("corrected: p50={:.3f}us p99={:.3f}us p99.9={:.3f}us (n={})", corrected_p50_us,
                 corrected_p99_us, corrected_p999_us, dist.corrected.sample_count());
    spdlog::info("results written to {}", (results_dir / "loaded-distribution.txt").string());

    return EXIT_SUCCESS;
}
