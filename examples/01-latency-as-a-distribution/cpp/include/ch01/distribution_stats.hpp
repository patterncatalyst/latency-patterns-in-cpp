#pragma once

#include <chrono>
#include <vector>

#include "measure/latency_recorder.hpp"

namespace ch01 {

// One raw scan-to-divert span, in seconds.
using Sample = std::chrono::duration<double>;

// The naive arithmetic mean of a raw sample set. This exists only so this
// chapter can show it next to the percentiles computed from the SAME
// samples and let the gap make the point (see CLAUDE.md, "Measurement
// discipline"): measure::LatencyRecorder deliberately carries no mean()
// method and never will, so this free function, not a recorder, computes
// the one number this chapter critiques. It is not a reported scan-to-divert
// result anywhere else in this guide.
//
// Throws std::logic_error if samples is empty (same contract as
// LatencyRecorder::percentile()).
[[nodiscard]] Sample compute_mean(const std::vector<Sample>& samples);

// Replaying one raw sample sequence into two recorders, rather than running
// the scenario twice, is what makes the comparison below a controlled
// before/after on a single run.
struct NaiveAndCorrected {
    measure::LatencyRecorder naive;      // expected_interval = 0: no correction
    measure::LatencyRecorder corrected;  // expected_interval = the belt's own arrival interval
};

// Builds the naive-versus-corrected pair described above. `expected_interval`
// is the belt's configured inter-arrival interval (1 / parcels_per_second);
// passing the same value into LatencyRecorder's constructor is what turns on
// Gil Tene's coordinated-omission backfill in `corrected` (see
// measure::LatencyRecorder::record()) while `naive` stays uncorrected.
[[nodiscard]] NaiveAndCorrected build_naive_and_corrected(const std::vector<Sample>& samples,
                                                           Sample expected_interval);

}  // namespace ch01
