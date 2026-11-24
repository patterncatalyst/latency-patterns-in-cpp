#include "line_simulator/line_simulator.hpp"

#include <algorithm>
#include <cstdint>
#include <random>

namespace line_simulator {

namespace {
constexpr int default_chute_count = 12;
}  // namespace

LineSimulator::LineSimulator(LineConfig config) : config_(config) {}

void LineSimulator::set_scan_to_divert_probe(std::shared_ptr<ScanToDivertProbe> probe) {
    probe_ = std::move(probe);
}

void LineSimulator::set_stage_work(Stage stage, StageWork work) {
    stage_work_[static_cast<std::size_t>(stage)] = std::move(work);
}

std::chrono::duration<double> LineSimulator::scan_to_divert_deadline() const {
    return std::chrono::duration<double>(config_.scan_to_diverter_distance_m /
                                          config_.belt_speed_m_per_s);
}

void LineSimulator::run_stage(Parcel& parcel, Stage stage) {
    auto& work = stage_work_[static_cast<std::size_t>(stage)];
    if (work) {
        work(parcel, stage);
    } else {
        default_stage_work(parcel, stage);
    }
}

void LineSimulator::default_stage_work(Parcel& parcel, Stage stage) {
    if (stage != Stage::Scan) {
        return;  // checkweigh, dimension, label, and sort have no default work
    }

    if (probe_) {
        probe_->on_sense(parcel, Clock::now());
    }

    // Baseline routing decision: a fixed divert table lookup. Later chapters
    // replace this stage's work via set_stage_work (Ch3 swaps it for a
    // precomputed O(1) lookup versus a per-parcel recompute); the point here
    // is only the hook the measurement sits on.
    parcel.chute = static_cast<int>(parcel.barcode % static_cast<std::uint64_t>(default_chute_count));

    if (probe_) {
        probe_->on_decide(parcel, Clock::now());
    }
    if (probe_) {
        probe_->on_act(parcel, Clock::now());
    }
}

std::uint64_t LineSimulator::run() {
    std::mt19937_64 rng{config_.random_seed};
    std::uniform_real_distribution<double> weight_dist{0.05, 25.0};  // kg
    std::uniform_real_distribution<double> dim_dist{5.0, 80.0};      // cm
    std::uniform_int_distribution<std::uint64_t> barcode_dist{1'000'000'000ULL,
                                                                9'999'999'999ULL};

    const auto start = Clock::now();
    const auto interval = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / config_.parcels_per_second));

    std::uint64_t completed = 0;
    for (std::uint64_t i = 0; i < config_.parcel_count; ++i) {
        Parcel parcel{
            .id = i,
            .barcode = barcode_dist(rng),
            .weight_kg = weight_dist(rng),
            .length_cm = dim_dist(rng),
            .width_cm = dim_dist(rng),
            .height_cm = dim_dist(rng),
            .chute = -1,
            .scheduled_arrival = start + interval * i,
        };

        for (Stage stage : stage_order) {
            run_stage(parcel, stage);
        }
        ++completed;
    }
    return completed;
}

}  // namespace line_simulator
