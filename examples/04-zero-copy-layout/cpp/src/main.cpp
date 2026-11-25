// examples/04-zero-copy-layout/cpp/src/main.cpp
//
// Two measured lanes in one run, both about where bytes live rather than how
// many instructions touch them (see CLAUDE.md, "Scope discipline": one
// running example per chapter, sub-techniques as lanes inside it).
//
//   1. Layout: the shared line-simulator's scan stage, twice, over the SAME
//      seeded parcel sequence, once with a recent-scan ledger built from
//      array-of-pointers-to-wide-structs (ParcelLedgerBefore) and once from
//      struct-of-arrays (ParcelLedgerAfter). Same decision either way (see
//      tests/layout_scan_stage_test.cpp); only the layout differs.
//   2. False sharing: two threads, one incrementing a "scan" counter and one
//      a "divert" counter, with the two counters first sharing a cache line
//      (UnpaddedCounters) and then not (PaddedCounters).
//
// Usage: ch04_zero_copy_layout <results-dir> <scrub-script> [python3]
//   Same contract as every prior chapter's example.
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ch04/false_sharing.hpp"
#include "ch04/layout_scan_stage.hpp"
#include "ch04/resolution_probe.hpp"
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

// Two-thread trial sizing for the false-sharing lane. Each trial's two
// threads increment their own counter this many times; 30 trials per layout
// is enough samples for a p50/p99/p99.9 read without letting the run drag
// (see CLAUDE.md, "Measurement discipline": report a distribution, never a
// mean).
constexpr std::uint64_t false_sharing_iterations_per_thread = 10'000'000;
constexpr int false_sharing_trial_count = 30;

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

LatencyRecorder recorder_from(const std::vector<ch04::Sample>& samples) {
    LatencyRecorder recorder;
    for (const ch04::Sample& sample : samples) {
        recorder.record(sample);
    }
    return recorder;
}

void write_lane(std::ostream& out, const char* key_prefix, const LatencyRecorder& recorder) {
    out << key_prefix << "_sample_count=" << recorder.sample_count() << '\n';
    out << key_prefix << "_p50_us=" << std::fixed << std::setprecision(3)
        << recorder.percentile(50) * 1e6 << '\n';
    out << key_prefix << "_p99_us=" << recorder.percentile(99) * 1e6 << '\n';
    out << key_prefix << "_p999_us=" << recorder.percentile(99.9) * 1e6 << '\n';
    out << "# " << key_prefix << " histogram (bucket range in microseconds, count, bar)\n";
    out << render_histogram(recorder.histogram());
}

// Runs trial_count trials of one false-sharing layout, recording each
// trial's wall-clock elapsed time as one sample (the same convention every
// other lane in this book uses: a measured elapsed time, never a derived
// rate). Returns the recorder plus the number of trials where either
// counter did not land on exactly iterations_per_thread, the behavioral
// check that the padding fix never drops or double-counts an increment.
struct FalseSharingLaneResult {
    LatencyRecorder recorder;
    int count_mismatches = 0;
};

template <typename TrialFn>
FalseSharingLaneResult run_false_sharing_lane(TrialFn trial_fn) {
    FalseSharingLaneResult result;
    for (int trial = 0; trial < false_sharing_trial_count; ++trial) {
        const ch04::FalseSharingTrialResult trial_result = trial_fn(false_sharing_iterations_per_thread);
        result.recorder.record(trial_result.elapsed);
        if (trial_result.scan_total != false_sharing_iterations_per_thread ||
            trial_result.divert_total != false_sharing_iterations_per_thread) {
            ++result.count_mismatches;
        }
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    measure::init_logging("ch04-zero-copy-layout");

    if (argc < 3) {
        spdlog::error("usage: ch04_zero_copy_layout <results-dir> <scrub-script> [python3]");
        return EXIT_FAILURE;
    }
    const fs::path results_dir = argv[1];
    const fs::path scrub_script = argv[2];
    const fs::path python3 = argc > 3 ? fs::path(argv[3]) : fs::path("python3");

    const std::string profile_header = read_profile_header(std::cin);

    fs::create_directories(results_dir);
    measure::ResultsDirScrubGuard scrub_guard(results_dir, scrub_script, python3);

    std::ofstream out(results_dir / "zero-copy-layout.txt", std::ios::trunc);
    out << "# Chapter 4 zero-copy-layout run: two unrelated measured effects,\n";
    out << "# both about where bytes live rather than how many instructions\n";
    out << "# touch them.\n";
    out << profile_header;

    // ---------------------------------------------------------------------
    // Lane 1: the recent-scan ledger, array-of-pointers-to-wide-structs
    // (before) versus struct-of-arrays (after). Same belt geometry as every
    // prior chapter; the default random_seed (1) is left untouched so both
    // runs see the identical parcel sequence.
    // ---------------------------------------------------------------------
    LineConfig config{};
    config.parcel_count = 5000;

    LineSimulator before_sim{config};
    auto before_probe = std::make_shared<ch04::ResolutionRecordingProbe>();
    ch04::BeforeLayoutScanStage before_stage{before_probe};
    before_sim.set_stage_work(Stage::Scan, std::ref(before_stage));
    spdlog::info("running {} parcels through the before (pointer-chasing AoS) ledger...",
                 config.parcel_count);
    const std::uint64_t before_completed = before_sim.run();
    spdlog::info("before: completed {} parcels, review_count={}", before_completed,
                 before_stage.review_count());

    LineSimulator after_sim{config};
    auto after_probe = std::make_shared<ch04::ResolutionRecordingProbe>();
    ch04::AfterLayoutScanStage after_stage{after_probe};
    after_sim.set_stage_work(Stage::Scan, std::ref(after_stage));
    spdlog::info("running {} parcels through the after (contiguous SoA) ledger...",
                 config.parcel_count);
    const std::uint64_t after_completed = after_sim.run();
    spdlog::info("after: completed {} parcels, review_count={}", after_completed,
                 after_stage.review_count());

    const std::vector<int>& before_chutes = before_probe->chutes();
    const std::vector<int>& after_chutes = after_probe->chutes();
    const std::size_t compare_count = std::min(before_chutes.size(), after_chutes.size());
    std::size_t chute_mismatches = 0;
    for (std::size_t i = 0; i < compare_count; ++i) {
        if (before_chutes[i] != after_chutes[i]) {
            ++chute_mismatches;
        }
    }

    const LatencyRecorder before_recorder = recorder_from(before_probe->samples());
    const LatencyRecorder after_recorder = recorder_from(after_probe->samples());

    out << "\n# --- lane 1: recent-scan ledger layout (AoS pointer-chasing vs SoA) ---\n";
    out << "parcel_count=" << config.parcel_count << '\n';
    out << "ledger_window=" << ch04::ledger_window << '\n';
    out << "key_space=" << ch04::key_space << '\n';
    out << "compared_chute_count=" << compare_count << '\n';
    out << "chute_mismatches=" << chute_mismatches << '\n';
    out << "before_review_count=" << before_stage.review_count() << '\n';
    out << "after_review_count=" << after_stage.review_count() << '\n';
    write_lane(out, "layout_before", before_recorder);
    write_lane(out, "layout_after", after_recorder);

    // ---------------------------------------------------------------------
    // Lane 2: false sharing between two counters, one updated per trial by
    // a thread standing in for the scan stage and one for the divert stage.
    // ---------------------------------------------------------------------
    spdlog::info("running {} unpadded false-sharing trials ({} increments/thread each)...",
                 false_sharing_trial_count, false_sharing_iterations_per_thread);
    const FalseSharingLaneResult unpadded = run_false_sharing_lane(ch04::run_unpadded_trial);
    spdlog::info("running {} padded false-sharing trials ({} increments/thread each)...",
                 false_sharing_trial_count, false_sharing_iterations_per_thread);
    const FalseSharingLaneResult padded = run_false_sharing_lane(ch04::run_padded_trial);

    out << "\n# --- lane 2: false sharing between two per-thread counters ---\n";
    out << "false_sharing_trial_count=" << false_sharing_trial_count << '\n';
    out << "false_sharing_iterations_per_thread=" << false_sharing_iterations_per_thread << '\n';
    out << "unpadded_count_mismatches=" << unpadded.count_mismatches << '\n';
    out << "padded_count_mismatches=" << padded.count_mismatches << '\n';
    write_lane(out, "unpadded", unpadded.recorder);
    write_lane(out, "padded", padded.recorder);

    const double before_p50_us = before_recorder.percentile(50) * 1e6;
    const double after_p50_us = after_recorder.percentile(50) * 1e6;
    const double unpadded_p50_us = unpadded.recorder.percentile(50) * 1e3;
    const double padded_p50_us = padded.recorder.percentile(50) * 1e3;

    spdlog::info("layout: before p50={:.3f}us after p50={:.3f}us (n={}/{})", before_p50_us,
                 after_p50_us, before_recorder.sample_count(), after_recorder.sample_count());
    spdlog::info("false sharing: unpadded p50={:.3f}ms padded p50={:.3f}ms (n={}/{})", unpadded_p50_us,
                 padded_p50_us, unpadded.recorder.sample_count(), padded.recorder.sample_count());
    spdlog::info("chute_mismatches={} (of {} compared)", chute_mismatches, compare_count);
    spdlog::info("results written to {}", (results_dir / "zero-copy-layout.txt").string());

    return EXIT_SUCCESS;
}
