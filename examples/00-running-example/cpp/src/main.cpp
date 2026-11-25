// examples/00-running-example/cpp/src/main.cpp
//
// Runs the shared line-simulator once, records scan-to-divert with the
// shared measurement harness, and writes a profile-labelled results file.
// This is the chapter's baseline: no technique is applied yet (see
// CLAUDE.md, "The spine"). Later chapters compare their lane's distribution
// against this one.
//
// Usage: ch00_running_example <results-dir> <scrub-script> [python3]
//   Reads scripts/profile.sh's KEY=value lines from stdin and copies them
//   verbatim into the results file header, so this binary never has to
//   reimplement host-profile detection (see scripts/profile.sh).
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

#include "ch00/recording_probe.hpp"
#include "line_simulator/line_simulator.hpp"
#include "measure/latency_recorder.hpp"
#include "measure/logging.hpp"
#include "measure/scrub.hpp"

namespace {

namespace fs = std::filesystem;
using line_simulator::LineConfig;
using line_simulator::LineSimulator;
using measure::Histogram;
using measure::LatencyRecorder;

// Reads every line on stdin unchanged: this is scripts/profile.sh's
// KEY=value output, piped in by examples/00-running-example/demo.sh.
std::string read_profile_header(std::istream& in) {
    std::ostringstream header;
    std::string line;
    while (std::getline(in, line)) {
        header << line << '\n';
    }
    return header.str();
}

// Renders an ASCII histogram: one row per bucket, a bar scaled to the
// busiest bucket so the shape of the distribution's tail is visible without
// plotting software. This is the "histogram or eCDF" CLAUDE.md requires
// (see "Per-chapter deliverables") next to the percentiles.
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
    measure::init_logging("ch00-running-example");

    if (argc < 3) {
        spdlog::error("usage: ch00_running_example <results-dir> <scrub-script> [python3]");
        return EXIT_FAILURE;
    }
    const fs::path results_dir = argv[1];
    const fs::path scrub_script = argv[2];
    const fs::path python3 = argc > 3 ? fs::path(argv[3]) : fs::path("python3");

    const std::string profile_header = read_profile_header(std::cin);

    fs::create_directories(results_dir);
    // Scrubs results_dir on scope exit, after the results file below is
    // written (see CLAUDE.md, "Public-repo host-id scrub rule"): the guard
    // is destructed last, at the end of main, so every file this run writes
    // gets one pass through scripts/scrub-host-ids.py before this process
    // exits.
    measure::ResultsDirScrubGuard scrub_guard(results_dir, scrub_script, python3);

    const LineConfig config{};  // the house default: see line_simulator.hpp
    LineSimulator sim{config};

    LatencyRecorder recorder;  // no expected_interval: this chapter does not
                                // yet apply coordinated-omission correction
                                // (that is the next chapter's lesson).
    auto probe = std::make_shared<ch00::RecordingProbe>(recorder);
    sim.set_scan_to_divert_probe(probe);

    spdlog::info("running {} parcels at {} parcels/s...", config.parcel_count,
                 config.parcels_per_second);
    const std::uint64_t completed = sim.run();
    spdlog::info("completed {} parcels, {} scan-to-divert samples recorded", completed,
                 recorder.sample_count());

    const double p50_us = recorder.percentile(50) * 1e6;
    const double p99_us = recorder.percentile(99) * 1e6;
    const double p999_us = recorder.percentile(99.9) * 1e6;
    const Histogram hist = recorder.histogram();
    const double deadline_us = sim.scan_to_divert_deadline().count() * 1e6;

    std::ofstream out(results_dir / "baseline.txt", std::ios::trunc);
    out << "# Chapter 0 baseline: scan-to-divert, uncorrected, no technique applied.\n";
    out << profile_header;
    out << "belt_speed_m_per_s=" << config.belt_speed_m_per_s << '\n';
    out << "scan_to_diverter_distance_m=" << config.scan_to_diverter_distance_m << '\n';
    out << "parcels_per_second=" << config.parcels_per_second << '\n';
    out << "parcel_count=" << config.parcel_count << '\n';
    out << "scan_to_divert_deadline_us=" << std::fixed << std::setprecision(3) << deadline_us << '\n';
    out << "sample_count=" << recorder.sample_count() << '\n';
    out << "p50_us=" << std::fixed << std::setprecision(3) << p50_us << '\n';
    out << "p99_us=" << std::fixed << std::setprecision(3) << p99_us << '\n';
    out << "p999_us=" << std::fixed << std::setprecision(3) << p999_us << '\n';
    out << "# histogram (bucket range in microseconds, count, bar)\n";
    out << render_histogram(hist);

    spdlog::info("p50={:.3f}us p99={:.3f}us p99.9={:.3f}us deadline={:.3f}us", p50_us, p99_us, p999_us,
                 deadline_us);
    spdlog::info("results written to {}", (results_dir / "baseline.txt").string());

    return EXIT_SUCCESS;
}
