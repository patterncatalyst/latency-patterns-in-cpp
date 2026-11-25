// examples/03-eliminating-work/cpp/src/main.cpp
//
// Drives the shared line-simulator twice, once with BeforeScanStage
// installed on the scan stage and once with AfterScanStage, over the SAME
// seeded parcel sequence (LineConfig::random_seed defaults to 1 and neither
// run changes it, so both simulators generate identical parcels in
// identical order). Records each lane's scan-to-divert distribution, and
// also the chute every parcel was finally resolved to, so this run can
// report chute_mismatches: 0 as a behavioral check that eliminating the
// per-parcel allocation, the map lookup, and the recomputed table changed
// nothing about what the line decides (see
// tests/before_after_divert_test.cpp for the same property, checked
// directly against the two stages rather than through a full run).
//
// Usage: ch03_eliminating_work <results-dir> <scrub-script> [python3]
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

#include "ch03/after_scan_stage.hpp"
#include "ch03/before_scan_stage.hpp"
#include "ch03/resolution_probe.hpp"
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

// Same ASCII histogram renderer as every prior chapter's example (see
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

LatencyRecorder recorder_from(const std::vector<ch03::Sample>& samples) {
    LatencyRecorder recorder;
    for (const ch03::Sample& sample : samples) {
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

}  // namespace

int main(int argc, char** argv) {
    measure::init_logging("ch03-eliminating-work");

    if (argc < 3) {
        spdlog::error("usage: ch03_eliminating_work <results-dir> <scrub-script> [python3]");
        return EXIT_FAILURE;
    }
    const fs::path results_dir = argv[1];
    const fs::path scrub_script = argv[2];
    const fs::path python3 = argc > 3 ? fs::path(argv[3]) : fs::path("python3");

    const std::string profile_header = read_profile_header(std::cin);

    fs::create_directories(results_dir);
    measure::ResultsDirScrubGuard scrub_guard(results_dir, scrub_script, python3);

    // Same belt geometry as every prior chapter (the budget is physical,
    // see CLAUDE.md, "The spine"); the default random_seed (1) is left
    // untouched so both runs below see the identical parcel sequence.
    LineConfig config{};
    config.parcel_count = 5000;

    LineSimulator before_sim{config};
    auto before_probe = std::make_shared<ch03::ResolutionRecordingProbe>();
    const ch03::BeforeScanStage before_stage{before_probe};
    before_sim.set_stage_work(Stage::Scan, before_stage);
    spdlog::info("running {} parcels through the before (unoptimized) scan decode path...",
                 config.parcel_count);
    const std::uint64_t before_completed = before_sim.run();
    spdlog::info("before: completed {} parcels", before_completed);

    LineSimulator after_sim{config};
    auto after_probe = std::make_shared<ch03::ResolutionRecordingProbe>();
    const ch03::AfterScanStage after_stage{after_probe};
    after_sim.set_stage_work(Stage::Scan, after_stage);
    spdlog::info("running {} parcels through the after (precomputed) scan decode path...",
                 config.parcel_count);
    const std::uint64_t after_completed = after_sim.run();
    spdlog::info("after: completed {} parcels", after_completed);

    // Behavioral verification: same seeded sequence in, so a mismatch here
    // would mean the two lanes disagree about a divert decision, not just
    // its cost.
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

    const double deadline_us = before_sim.scan_to_divert_deadline().count() * 1e6;

    std::ofstream out(results_dir / "eliminating-work.txt", std::ios::trunc);
    out << "# Chapter 3 eliminating-work run: the scan decode path measured\n";
    out << "# twice over the SAME seeded parcel sequence. before = per-parcel\n";
    out << "# heap allocation + std::unordered_map lookup + a from-scratch\n";
    out << "# zone-multiplier rebuild (see ch03::BeforeScanStage); after = a\n";
    out << "# precomputed std::array lookup, a hoisted zone-multiplier table,\n";
    out << "# and no per-parcel allocation (see ch03::AfterScanStage).\n";
    out << "# chute_mismatches is this run's behavioral check: both lanes see\n";
    out << "# the identical parcel sequence and must resolve the identical\n";
    out << "# chute for every one of them.\n";
    out << profile_header;
    out << "belt_speed_m_per_s=" << config.belt_speed_m_per_s << '\n';
    out << "scan_to_diverter_distance_m=" << config.scan_to_diverter_distance_m << '\n';
    out << "parcel_count=" << config.parcel_count << '\n';
    out << "scan_to_divert_deadline_us=" << std::fixed << std::setprecision(3) << deadline_us << '\n';
    out << "compared_chute_count=" << compare_count << '\n';
    out << "chute_mismatches=" << chute_mismatches << '\n';

    write_lane(out, "before", before_recorder);
    write_lane(out, "after", after_recorder);

    const double before_p50_us = before_recorder.percentile(50) * 1e6;
    const double after_p50_us = after_recorder.percentile(50) * 1e6;
    const double before_p999_us = before_recorder.percentile(99.9) * 1e6;
    const double after_p999_us = after_recorder.percentile(99.9) * 1e6;

    spdlog::info("before: p50={:.3f}us p99={:.3f}us p99.9={:.3f}us (n={})", before_p50_us,
                 before_recorder.percentile(99) * 1e6, before_p999_us, before_recorder.sample_count());
    spdlog::info("after:  p50={:.3f}us p99={:.3f}us p99.9={:.3f}us (n={})", after_p50_us,
                 after_recorder.percentile(99) * 1e6, after_p999_us, after_recorder.sample_count());
    spdlog::info("chute_mismatches={} (of {} compared)", chute_mismatches, compare_count);
    spdlog::info("results written to {}", (results_dir / "eliminating-work.txt").string());

    return EXIT_SUCCESS;
}
