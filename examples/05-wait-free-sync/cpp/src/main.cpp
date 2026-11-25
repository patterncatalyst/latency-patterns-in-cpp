// examples/05-wait-free-sync/cpp/src/main.cpp
//
// One handoff, two queues: the scan thread produces a Decision per parcel,
// the divert thread consumes it, and the queue between them is swapped
// between a mutex-guarded std::queue (the "before") and a wait-free SPSC
// ring buffer (the "after"). Both lanes run the identical item count with
// both threads flat out, no pacing, so the queue itself is the only thing
// that differs (see CLAUDE.md, "The spine": one running example, swap only
// the technique under test).
//
// Usage: ch05_wait_free_sync <results-dir> <scrub-script> [python3]
//   Same contract as every prior chapter's example.
#include <spdlog/spdlog.h>

#include <algorithm>
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

#include "ch05/handoff_trial.hpp"
#include "measure/latency_recorder.hpp"
#include "measure/logging.hpp"
#include "measure/scrub.hpp"

namespace {

namespace fs = std::filesystem;
using measure::Histogram;
using measure::LatencyRecorder;

// Enough items for a stable p99.9 read without letting the mutex lane's
// condition_variable traffic drag the run out (see CLAUDE.md, "Measurement
// discipline": report a distribution, never a mean).
constexpr std::uint64_t handoff_item_count = 200'000;

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

LatencyRecorder recorder_from(const std::vector<double>& latencies_seconds) {
    LatencyRecorder recorder;
    for (const double latency : latencies_seconds) {
        recorder.record(std::chrono::duration<double>(latency));
    }
    return recorder;
}

void write_lane(std::ostream& out, const char* key_prefix, const ch05::HandoffTrialResult& trial) {
    const LatencyRecorder recorder = recorder_from(trial.latencies_seconds);
    out << key_prefix << "_items_sent=" << trial.items_sent << '\n';
    out << key_prefix << "_items_received=" << trial.items_received << '\n';
    out << key_prefix << "_dropped_count=" << trial.dropped_count << '\n';
    out << key_prefix << "_out_of_order_count=" << trial.out_of_order_count << '\n';
    out << key_prefix << "_duplicate_count=" << trial.duplicate_count << '\n';
    out << key_prefix << "_full_retries=" << trial.full_retries << '\n';
    out << key_prefix << "_sample_count=" << recorder.sample_count() << '\n';
    out << key_prefix << "_p50_us=" << std::fixed << std::setprecision(3)
        << recorder.percentile(50) * 1e6 << '\n';
    out << key_prefix << "_p99_us=" << recorder.percentile(99) * 1e6 << '\n';
    out << key_prefix << "_p999_us=" << recorder.percentile(99.9) * 1e6 << '\n';
    out << "# " << key_prefix << " histogram (bucket range in microseconds, count, bar)\n";
    out << render_histogram(recorder.histogram());
}

}  // namespace

int main(int argc, char** argv) {
    measure::init_logging("ch05-wait-free-sync");

    if (argc < 3) {
        spdlog::error("usage: ch05_wait_free_sync <results-dir> <scrub-script> [python3]");
        return EXIT_FAILURE;
    }
    const fs::path results_dir = argv[1];
    const fs::path scrub_script = argv[2];
    const fs::path python3 = argc > 3 ? fs::path(argv[3]) : fs::path("python3");

    const std::string profile_header = read_profile_header(std::cin);

    fs::create_directories(results_dir);
    measure::ResultsDirScrubGuard scrub_guard(results_dir, scrub_script, python3);

    spdlog::info("running {} items through the mutex-guarded handoff queue...", handoff_item_count);
    const ch05::HandoffTrialResult mutex_result = ch05::run_mutex_handoff_trial(handoff_item_count);
    spdlog::info("mutex: received {}/{}, dropped={}, out_of_order={}, duplicate={}",
                 mutex_result.items_received, mutex_result.items_sent, mutex_result.dropped_count,
                 mutex_result.out_of_order_count, mutex_result.duplicate_count);

    spdlog::info("running {} items through the wait-free SPSC ring...", handoff_item_count);
    const ch05::HandoffTrialResult ring_result = ch05::run_ring_handoff_trial(handoff_item_count);
    spdlog::info("ring: received {}/{}, dropped={}, out_of_order={}, duplicate={}, full_retries={}",
                 ring_result.items_received, ring_result.items_sent, ring_result.dropped_count,
                 ring_result.out_of_order_count, ring_result.duplicate_count, ring_result.full_retries);

    std::ofstream out(results_dir / "wait-free-sync.txt", std::ios::trunc);
    out << "# Chapter 5 wait-free-sync run: the same scan-to-divert handoff,\n";
    out << "# measured once through ch05::MutexHandoffQueue (a mutex +\n";
    out << "# condition_variable around a std::queue) and once through\n";
    out << "# ch05::SpscRingQueue (a fixed-capacity, wait-free ring with\n";
    out << "# acquire/release ordered head/tail indices). Both lanes run the\n";
    out << "# identical item count with both threads flat out, no pacing.\n";
    out << "# *_dropped_count, *_out_of_order_count, and *_duplicate_count are\n";
    out << "# this run's behavioral check: every item sent exactly once must\n";
    out << "# arrive exactly once, in the order it was sent.\n";
    out << profile_header;
    out << "handoff_item_count=" << handoff_item_count << '\n';
    out << "ring_capacity=" << ch05::ring_capacity << '\n';

    out << "\n# --- lane 1: mutex-guarded queue (condition_variable, blocking) ---\n";
    write_lane(out, "handoff_mutex", mutex_result);

    out << "\n# --- lane 2: wait-free SPSC ring (atomics, acquire/release) ---\n";
    write_lane(out, "handoff_ring", ring_result);

    const LatencyRecorder mutex_recorder = recorder_from(mutex_result.latencies_seconds);
    const LatencyRecorder ring_recorder = recorder_from(ring_result.latencies_seconds);

    spdlog::info("mutex: p50={:.3f}us p99={:.3f}us p99.9={:.3f}us (n={})",
                 mutex_recorder.percentile(50) * 1e6, mutex_recorder.percentile(99) * 1e6,
                 mutex_recorder.percentile(99.9) * 1e6, mutex_recorder.sample_count());
    spdlog::info("ring:  p50={:.3f}us p99={:.3f}us p99.9={:.3f}us (n={})",
                 ring_recorder.percentile(50) * 1e6, ring_recorder.percentile(99) * 1e6,
                 ring_recorder.percentile(99.9) * 1e6, ring_recorder.sample_count());
    spdlog::info("results written to {}", (results_dir / "wait-free-sync.txt").string());

    return EXIT_SUCCESS;
}
