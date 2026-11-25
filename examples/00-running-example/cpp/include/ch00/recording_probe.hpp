#pragma once

#include <chrono>

#include "line_simulator/parcel.hpp"
#include "line_simulator/scan_to_divert_probe.hpp"
#include "measure/latency_recorder.hpp"

namespace ch00 {

// Adapts the line-simulator's sense/decide/act callbacks to the shared
// measure::LatencyRecorder. The spine metric is the full sense-to-act span
// (see CLAUDE.md, "The spine": scan-to-divert, sense -> decide -> act), so
// on_decide is not wired to anything: it exists on the interface for a later
// chapter that needs the decide boundary on its own (Ch2's per-stage
// decomposition), not for this chapter's headline number.
//
// One RecordingProbe instance serves the whole run, not one per parcel: the
// line-simulator processes each parcel's three callbacks back to back before
// moving to the next (see line_simulator.cpp, run()), so a single
// sense_at_ member is never overwritten by a different parcel's sense before
// its own act arrives.
class RecordingProbe : public line_simulator::ScanToDivertProbe {
public:
    explicit RecordingProbe(measure::LatencyRecorder& recorder) : recorder_(recorder) {}

    void on_sense(const line_simulator::Parcel& /*parcel*/,
                  line_simulator::Clock::time_point at) override {
        sense_at_ = at;
    }

    void on_decide(const line_simulator::Parcel& /*parcel*/,
                   line_simulator::Clock::time_point /*at*/) override {
        // Intentionally not recorded here; see class comment.
    }

    void on_act(const line_simulator::Parcel& /*parcel*/,
                line_simulator::Clock::time_point at) override {
        recorder_.record(at - sense_at_);
    }

private:
    measure::LatencyRecorder& recorder_;
    line_simulator::Clock::time_point sense_at_;
};

}  // namespace ch00
