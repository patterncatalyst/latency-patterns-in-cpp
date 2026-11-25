#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ch02/pipeline_timing.hpp"
#include "line_simulator/parcel.hpp"

namespace ch02 {

// The chute count scan_work assigns into; SortDispatcher is sized against
// this same constant so every chute scan_work can pick has a counter.
inline constexpr int chute_count = 12;

// The five stage workloads this chapter measures. Each one is a real,
// idiomatic cost tied to a concrete source of latency on this line (see
// the chapter prose, "Sources of latency on this line"); none of them is a
// stand-in sleep. Checkweigh and dimension filter a noisy reading the same
// way, over one value and three, so their costs are directly comparable;
// scan is deliberately the heaviest, since it is this chapter's Amdahl's
// Law target.

// Averages several synthetic scale readings instead of trusting one, the
// measurable CPU/hardware cost of filtering a physically noisy load-cell
// signal. The settle time of the physical sensor itself is a latency
// source this book cannot measure from software (see "Sources of latency
// on this line"); this function is only the filtering cost paid after
// that signal already exists.
void checkweigh_work(line_simulator::Parcel& parcel);

// The same noise-averaging shape as checkweigh_work, run once per
// dimension (length, width, height), standing in for a dimensioner's
// multiple laser or camera sweeps.
void dimension_work(line_simulator::Parcel& parcel);

// A barcode checksum decode followed by a chute assignment. This is the
// heaviest of the five by a wide margin, by design: it is the stage a
// later chapter's "eliminating work" technique targets, and the stage
// this chapter's Amdahl's Law section asks "what if half of this ran on
// two cores instead of one" about. Neither question is answered here;
// this function only produces the real, measured cost those questions
// are about.
void scan_work(line_simulator::Parcel& parcel);

// Builds the print line a label printer would receive and keeps the most
// recently rendered line as its own state, standing in for a printer's
// output buffer. The printer's own render-and-spool time is a driver/OS
// cost this book cannot isolate from here; this type is only the
// application-side formatting cost of building that line.
class LabelRenderer {
public:
    void operator()(line_simulator::Parcel& parcel);

    [[nodiscard]] const std::string& last_rendered() const noexcept { return last_rendered_; }

private:
    std::string last_rendered_;
};

// Per-chute dispatch bookkeeping: counts how many parcels this run routed
// to each chute, standing in for the diverter controller updating its own
// occupancy state once it acts on a routing decision.
class SortDispatcher {
public:
    explicit SortDispatcher(int num_chutes);

    void operator()(line_simulator::Parcel& parcel);

    [[nodiscard]] const std::vector<std::uint64_t>& chute_counts() const noexcept {
        return chute_counts_;
    }

private:
    std::vector<std::uint64_t> chute_counts_;
};

// Wires the five workloads above into one StageWorkloads. `label` and
// `sort` are passed by reference so a caller (including a test) can
// inspect their state, LabelRenderer::last_rendered() and
// SortDispatcher::chute_counts(), after the run; checkweigh_work,
// dimension_work, and scan_work are free functions with no state to
// inspect.
StageWorkloads make_stage_workloads(LabelRenderer& label, SortDispatcher& sort);

}  // namespace ch02
