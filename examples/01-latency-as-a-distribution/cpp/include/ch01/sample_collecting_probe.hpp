#pragma once

#include <chrono>
#include <vector>

#include "ch01/distribution_stats.hpp"
#include "line_simulator/parcel.hpp"
#include "line_simulator/scan_to_divert_probe.hpp"

namespace ch01 {

// Adapts the line-simulator's sense/decide/act callbacks to a raw sample
// vector instead of straight into a measure::LatencyRecorder (contrast with
// Chapter 0's RecordingProbe, which records straight into one recorder):
// this chapter replays the same raw sequence into two recorders afterward
// (see build_naive_and_corrected), so the probe's only job is to capture
// that one sequence once.
//
// One instance serves the whole run, the same reasoning as Chapter 0's
// RecordingProbe: LoadedScanStage drives sense/decide/act back to back for
// one parcel before the line-simulator moves to the next, so a single
// sense_at_ member is never overwritten by a different parcel's sense before
// its own act arrives.
class SampleCollectingProbe : public line_simulator::ScanToDivertProbe {
public:
    void on_sense(const line_simulator::Parcel& /*parcel*/,
                  line_simulator::Clock::time_point at) override {
        sense_at_ = at;
    }

    void on_decide(const line_simulator::Parcel& /*parcel*/,
                   line_simulator::Clock::time_point /*at*/) override {
        // Not recorded; see Chapter 0's RecordingProbe for why the spine
        // metric is the full sense-to-act span rather than either half.
    }

    void on_act(const line_simulator::Parcel& /*parcel*/,
                line_simulator::Clock::time_point at) override {
        samples_.emplace_back(at - sense_at_);
    }

    [[nodiscard]] const std::vector<Sample>& samples() const noexcept { return samples_; }

private:
    line_simulator::Clock::time_point sense_at_;
    std::vector<Sample> samples_;
};

}  // namespace ch01
