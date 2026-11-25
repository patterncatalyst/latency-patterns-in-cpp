#pragma once

#include <chrono>
#include <vector>

#include "line_simulator/parcel.hpp"
#include "line_simulator/scan_to_divert_probe.hpp"

namespace ch03 {

// Same shape as Chapter 1's Sample (see ch01::Sample): a raw duration, not
// yet fed into a measure::LatencyRecorder.
using Sample = std::chrono::duration<double>;

// Records the scan-to-divert span for one parcel, the same sense-to-act
// span ch01::SampleCollectingProbe records, plus the chute that parcel was
// finally resolved to. BeforeScanStage and AfterScanStage both call on_act
// after writing parcel.chute (see their operator()), so this probe doubles
// as this chapter's behavioral check: main.cpp runs both lanes over the
// same seeded parcel sequence and compares the two chute sequences
// parcel for parcel.
class ResolutionRecordingProbe : public line_simulator::ScanToDivertProbe {
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

    void on_act(const line_simulator::Parcel& parcel, line_simulator::Clock::time_point at) override {
        samples_.emplace_back(at - sense_at_);
        chutes_.push_back(parcel.chute);
    }

    [[nodiscard]] const std::vector<Sample>& samples() const noexcept { return samples_; }
    [[nodiscard]] const std::vector<int>& chutes() const noexcept { return chutes_; }

private:
    line_simulator::Clock::time_point sense_at_;
    std::vector<Sample> samples_;
    std::vector<int> chutes_;
};

}  // namespace ch03
