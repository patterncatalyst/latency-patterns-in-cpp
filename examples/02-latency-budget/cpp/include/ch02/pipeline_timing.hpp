#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <numeric>
#include <vector>

#include "line_simulator/line_simulator.hpp"
#include "line_simulator/parcel.hpp"
#include "line_simulator/stage.hpp"

namespace ch02 {

// One stage's own measured span, in seconds. Same shape as Chapter 1's
// Sample (see ch01::Sample): a raw duration, not yet fed into a
// measure::LatencyRecorder.
using Sample = std::chrono::duration<double>;

// A chapter-local per-stage workload: the five callables LineSimulator
// drives once per parcel at each stage, in stage_order. Named fields,
// rather than an array keyed by Stage, read directly against the five
// sources-of-latency sections in the chapter prose.
struct StageWorkloads {
    std::function<void(line_simulator::Parcel&)> checkweigh;
    std::function<void(line_simulator::Parcel&)> dimension;
    std::function<void(line_simulator::Parcel&)> scan;
    std::function<void(line_simulator::Parcel&)> label;
    std::function<void(line_simulator::Parcel&)> sort;
};

// Installs a real, timed workload on every one of the five stages and
// records each stage's own cost as a separate lane. This is new relative
// to Chapter 0 and Chapter 1: both of those left checkweigh, dimension,
// label, and sort at the simulator's default (a no-op), because only the
// scan stage's cost mattered to the question either chapter was asking.
// This chapter needs a real, measurable cost at every stage to say
// anything concrete about where a latency budget goes.
//
// `set_stage_work` replaces one stage's default work per call (see
// line_simulator.hpp), so install() wires one small timing lambda per
// stage rather than one lambda that branches on which stage it was given;
// each lambda already knows, from how it was registered, which stage it
// times.
//
// scan_to_divert_samples() sums the three stages that sit inside the
// physical scan-to-divert window: a parcel's checkweigh and dimension
// readings finish before it ever reaches the scanner, so neither one eats
// into the budget distance / belt_speed describes (see CLAUDE.md, "The
// spine"); scan, label, and sort are what has to complete, in sequence,
// once the scanner has sensed the parcel. The sum is built directly from
// the three measured spans that produced it, never re-measured
// end-to-end, so "the parts add up to the total" is true by construction.
class PipelineTimingHarness {
public:
    // Registers one timing wrapper per stage via sim.set_stage_work.
    // Must be called before sim.run().
    void install(line_simulator::LineSimulator& sim, StageWorkloads workloads);

    [[nodiscard]] const std::vector<Sample>& stage_samples(line_simulator::Stage stage) const {
        return stage_samples_[static_cast<std::size_t>(stage)];
    }

    [[nodiscard]] const std::vector<Sample>& scan_to_divert_samples() const noexcept {
        return scan_to_divert_samples_;
    }

private:
    void time_stage(line_simulator::Parcel& parcel, line_simulator::Stage stage,
                     const std::function<void(line_simulator::Parcel&)>& work);

    std::array<std::vector<Sample>, line_simulator::stage_count> stage_samples_{};
    std::array<Sample, line_simulator::stage_count> current_parcel_{};
    std::vector<Sample> scan_to_divert_samples_;
};

}  // namespace ch02
