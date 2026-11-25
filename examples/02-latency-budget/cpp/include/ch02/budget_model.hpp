#pragma once

#include <vector>

#include "ch02/pipeline_timing.hpp"

namespace ch02 {

// The arithmetic mean of a raw sample set. Kept outside measure::
// LatencyRecorder for the same reason Chapter 1's compute_mean is (see
// ch01::compute_mean): a mean is never a reported latency in this guide
// (see CLAUDE.md, "Measurement discipline"). The one place it is allowed
// to appear at all is as a labelled modelling input to Little's Law or
// Amdahl's Law, which is this chapter, and this function exists to
// compute exactly that input, never a result reported on its own.
//
// Throws std::logic_error if samples is empty.
[[nodiscard]] Sample compute_mean(const std::vector<Sample>& samples);

// Little's Law: L = lambda * W. Given an arrival rate (parcels per
// second) and the time a parcel spends in the segment being asked about
// (seconds), returns the expected number of parcels in that segment at
// any instant. This is a planning estimate from two input numbers, never
// a count of parcels this chapter actually observed in flight at once.
[[nodiscard]] double little_law_in_flight_estimate(double arrival_rate_per_s,
                                                    double residence_time_s);

// Amdahl's Law: the speedup ceiling from running a fraction `p` of a
// pipeline on `workers` parallel workers while the rest stays serial:
// speedup = 1 / ((1 - p) + p / workers). `p` must be in [0, 1] and
// `workers` must be at least 1; both are asserted, not caught, since a
// caller passing an invalid fraction or worker count is a programming
// error, not a runtime condition this chapter's model needs to recover
// from. This is a planning ceiling, not a measurement: nothing in this
// chapter's example actually splits a stage across workers.
[[nodiscard]] double amdahl_speedup(double parallel_fraction, double workers);

}  // namespace ch02
