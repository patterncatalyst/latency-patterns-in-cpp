// examples/02-latency-budget/cpp/src/main.cpp
//
// Drives the shared line-simulator with a real, timed workload at every one
// of the five stages (see ch02::PipelineTimingHarness), and writes two kinds
// of number to the results file: a measured distribution per stage, and a
// handful of modelling outputs (Little's Law, Amdahl's Law) computed from
// those measurements but never reported as measured latencies themselves
// (see CLAUDE.md, "Measurement discipline").
//
// Usage: ch02_latency_budget <results-dir> <scrub-script> [python3]
//   Same contract as Chapter 0 and Chapter 1's examples: reads
//   scripts/profile.sh's KEY=value lines from stdin.
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ch02/budget_model.hpp"
#include "ch02/pipeline_timing.hpp"
#include "ch02/stage_workloads.hpp"
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

// Same ASCII histogram renderer as Chapter 0 and Chapter 1's examples (see
// ch01's main.cpp for why this stays chapter-local rather than moving into
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

LatencyRecorder recorder_from(const std::vector<ch02::Sample>& samples) {
    LatencyRecorder recorder;
    for (const ch02::Sample& sample : samples) {
        recorder.record(sample);
    }
    return recorder;
}

struct StageReport {
    const char* name = nullptr;
    LatencyRecorder recorder;
};

void write_lane(std::ostream& out, const char* key_prefix, const LatencyRecorder& recorder) {
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
    measure::init_logging("ch02-latency-budget");

    if (argc < 3) {
        spdlog::error("usage: ch02_latency_budget <results-dir> <scrub-script> [python3]");
        return EXIT_FAILURE;
    }
    const fs::path results_dir = argv[1];
    const fs::path scrub_script = argv[2];
    const fs::path python3 = argc > 3 ? fs::path(argv[3]) : fs::path("python3");

    const std::string profile_header = read_profile_header(std::cin);

    fs::create_directories(results_dir);
    measure::ResultsDirScrubGuard scrub_guard(results_dir, scrub_script, python3);

    // Same belt geometry as every prior chapter (the budget is physical,
    // see CLAUDE.md, "The spine"); only the parcel count changes, to give
    // every stage enough samples for a stable p99.9.
    LineConfig config{};
    config.parcel_count = 5000;

    LineSimulator sim{config};

    ch02::LabelRenderer label;
    ch02::SortDispatcher sort{ch02::chute_count};
    ch02::PipelineTimingHarness harness;
    harness.install(sim, ch02::make_stage_workloads(label, sort));

    spdlog::info("running {} parcels past a timed checkweigh/dimension/scan/label/sort...",
                 config.parcel_count);
    const std::uint64_t completed = sim.run();
    spdlog::info("completed {} parcels", completed);

    const auto stage_report = [&harness](const char* name, Stage stage) {
        return StageReport{.name = name, .recorder = recorder_from(harness.stage_samples(stage))};
    };
    const std::array<StageReport, 5> stages{
        stage_report("checkweigh", Stage::Checkweigh),
        stage_report("dimension", Stage::Dimension),
        stage_report("scan", Stage::Scan),
        stage_report("label", Stage::Label),
        stage_report("sort", Stage::Sort),
    };
    const LatencyRecorder scan_to_divert = recorder_from(harness.scan_to_divert_samples());

    // --- Little's Law and Amdahl's Law: modelling inputs only, never a
    // reported measurement (see CLAUDE.md, "Measurement discipline"). Both
    // means below feed a model, and neither is printed as a scan-to-divert
    // result anywhere else in this guide.
    const double scan_mean_us = ch02::compute_mean(harness.stage_samples(Stage::Scan)).count() * 1e6;
    const double scan_to_divert_mean_us =
        ch02::compute_mean(harness.scan_to_divert_samples()).count() * 1e6;
    const double amdahl_parallel_fraction = scan_mean_us / scan_to_divert_mean_us;

    const double deadline_s = sim.scan_to_divert_deadline().count();
    const double deadline_us = deadline_s * 1e6;
    const double little_law_default_estimate =
        ch02::little_law_in_flight_estimate(config.parcels_per_second, deadline_s);
    constexpr double loaded_arrival_rate_per_s = 2000.0;  // Chapter 1's stress-test arrival rate
    const double little_law_loaded_estimate =
        ch02::little_law_in_flight_estimate(loaded_arrival_rate_per_s, deadline_s);

    constexpr std::array<double, 3> amdahl_worker_counts{2.0, 4.0, 8.0};
    std::array<double, 3> amdahl_speedups{};
    for (std::size_t i = 0; i < amdahl_worker_counts.size(); ++i) {
        amdahl_speedups[i] = ch02::amdahl_speedup(amdahl_parallel_fraction, amdahl_worker_counts[i]);
    }
    const double amdahl_ceiling = 1.0 / (1.0 - amdahl_parallel_fraction);

    std::ofstream out(results_dir / "budget-decomposition.txt", std::ios::trunc);
    out << "# Chapter 2 budget-decomposition run: a real, timed workload at\n";
    out << "# every one of the five stages (see ch02::PipelineTimingHarness).\n";
    out << "# checkweigh/dimension/scan/label/sort are each their own measured\n";
    out << "# lane; scan_to_divert sums scan+label+sort (the three stages that\n";
    out << "# run at or after the scanner, inside the physical scan-to-divert\n";
    out << "# window); checkweigh and dimension finish before the parcel\n";
    out << "# reaches the scanner, so they are reported but excluded from that\n";
    out << "# sum. model_* lines are Little's Law / Amdahl's Law planning\n";
    out << "# estimates, never a reported latency (see CLAUDE.md, \"Measurement\n";
    out << "# discipline\").\n";
    out << profile_header;
    out << "belt_speed_m_per_s=" << config.belt_speed_m_per_s << '\n';
    out << "scan_to_diverter_distance_m=" << config.scan_to_diverter_distance_m << '\n';
    out << "parcels_per_second=" << config.parcels_per_second << '\n';
    out << "parcel_count=" << config.parcel_count << '\n';
    out << "scan_to_divert_deadline_us=" << std::fixed << std::setprecision(3) << deadline_us << '\n';

    for (const StageReport& stage : stages) {
        write_lane(out, stage.name, stage.recorder);
    }
    out << "# scan_to_divert: scan + label + sort, summed per parcel from the\n";
    out << "# three measured spans above, never re-measured end to end\n";
    write_lane(out, "scan_to_divert", scan_to_divert);

    out << "# model_little_law: L = lambda * W, lambda = arrival rate, W = the\n";
    out << "# scan-to-divert deadline (the time a parcel spends in the segment\n";
    out << "# between the scanner and the diverter). A planning estimate, not a\n";
    out << "# count this run actually observed in flight.\n";
    out << "model_little_law_w_s=" << std::fixed << std::setprecision(6) << deadline_s << '\n';
    out << "model_little_law_default_lambda_per_s=" << config.parcels_per_second << '\n';
    out << "model_little_law_default_L_estimate=" << std::setprecision(3) << little_law_default_estimate
        << '\n';
    out << "model_little_law_loaded_lambda_per_s=" << loaded_arrival_rate_per_s << '\n';
    out << "model_little_law_loaded_L_estimate=" << little_law_loaded_estimate << '\n';

    out << "# model_amdahl: speedup = 1 / ((1 - p) + p / workers), p = this\n";
    out << "# run's measured scan-stage mean divided by its measured\n";
    out << "# scan_to_divert mean (both means are modelling inputs only, never\n";
    out << "# reported as latencies elsewhere in this guide).\n";
    out << "model_amdahl_scan_mean_us=" << scan_mean_us << '\n';
    out << "model_amdahl_scan_to_divert_mean_us=" << scan_to_divert_mean_us << '\n';
    out << "model_amdahl_parallel_fraction=" << std::setprecision(4) << amdahl_parallel_fraction
        << '\n';
    for (std::size_t i = 0; i < amdahl_worker_counts.size(); ++i) {
        out << "model_amdahl_speedup_" << static_cast<int>(amdahl_worker_counts[i]) << "workers="
            << std::setprecision(3) << amdahl_speedups[i] << '\n';
    }
    out << "model_amdahl_speedup_ceiling=" << amdahl_ceiling << '\n';

    spdlog::info("scan_to_divert: p50={:.3f}us p99={:.3f}us p99.9={:.3f}us (n={})",
                 scan_to_divert.percentile(50) * 1e6, scan_to_divert.percentile(99) * 1e6,
                 scan_to_divert.percentile(99.9) * 1e6, scan_to_divert.sample_count());
    spdlog::info("model: little's law default L={:.3f}, loaded L={:.3f}; amdahl p={:.4f}, ceiling={:.3f}",
                 little_law_default_estimate, little_law_loaded_estimate, amdahl_parallel_fraction, amdahl_ceiling);
    spdlog::info("results written to {}", (results_dir / "budget-decomposition.txt").string());

    return EXIT_SUCCESS;
}
