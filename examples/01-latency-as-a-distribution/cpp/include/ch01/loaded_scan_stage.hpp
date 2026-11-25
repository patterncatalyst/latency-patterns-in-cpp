#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include "line_simulator/parcel.hpp"
#include "line_simulator/scan_to_divert_probe.hpp"
#include "line_simulator/stage.hpp"

namespace ch01 {

// How often, and for how long, the scan stage stalls. `every_n_parcels == 0`
// disables the stall entirely (every parcel takes the fast path only),
// which the tests use to isolate the decode cost from the stall cost.
struct StallSchedule {
    std::uint64_t every_n_parcels = 250;
    std::chrono::duration<double> duration = std::chrono::milliseconds(8);
};

// Chapter 1's scan-stage workload: real decode work on every parcel, plus a
// periodic stall standing in for a GC-like pause or a downstream stage (the
// label printer's spooler, a lock a later chapter moves off the hot path)
// briefly blocking this one. Installed via
// LineSimulator::set_stage_work(Stage::Scan, ...): overriding a stage skips
// the simulator's own default_stage_work, including its probe calls (see
// line_simulator.hpp's set_scan_to_divert_probe doc comment), so this type
// drives sense/decide/act itself, in the same order Chapter 0's
// RecordingProbe expects them.
//
// The belt does not know or care that this stage sometimes stalls: the next
// parcel's scheduled_arrival was fixed before this parcel's scan even
// started (see parcel.hpp). Every arrival that would have piled up behind a
// stall is invisible to a probe that only records the one span it measured,
// which is exactly the gap build_naive_and_corrected's correction exists to
// repair.
class LoadedScanStage {
public:
    LoadedScanStage(std::shared_ptr<line_simulator::ScanToDivertProbe> probe, StallSchedule stall)
        : probe_(std::move(probe)), stall_(stall) {}

    void operator()(line_simulator::Parcel& parcel, line_simulator::Stage stage) const {
        using line_simulator::Clock;
        if (stage != line_simulator::Stage::Scan) {
            return;  // every other stage keeps the house default (a no-op)
        }

        probe_->on_sense(parcel, Clock::now());

        // The decode: a barcode checksum standing in for the symbology and
        // manifest cross-checks a real scanner's decode pass performs, plus
        // a dimensional-weight check (length x width x height / 5000, the
        // courier-industry volumetric-weight formula) that routes an
        // oversize-for-its-weight parcel to its own chute. Chapter 3
        // ("eliminating work") is what later replaces a decode like this
        // with a precomputed lookup; this chapter's point is only that the
        // decode has a real, measurable, non-zero cost.
        std::uint64_t checksum = parcel.barcode;
        for (int i = 0; i < decode_iterations; ++i) {
            checksum = (checksum * hash_multiplier) ^ (checksum >> hash_shift);
        }

        const double volumetric_weight_kg =
            (parcel.length_cm * parcel.width_cm * parcel.height_cm) / volumetric_divisor_cm3_per_kg;
        const bool oversize_for_weight = volumetric_weight_kg > parcel.weight_kg * oversize_margin;

        parcel.chute = oversize_for_weight
                           ? oversize_chute
                           : static_cast<int>(checksum % static_cast<std::uint64_t>(chute_count));

        // The stall: every stall_.every_n_parcels-th parcel pays a cost
        // orders of magnitude above the decode above. The parcels behind it
        // in this run were already scheduled to arrive on their own clock
        // (see parcel.hpp's scheduled_arrival), so none of them is waiting
        // for this one to clear; a probe that only records this one span
        // reports it as a single slow sample, not as the pile-up it implies.
        if (stall_.every_n_parcels > 0 && parcel.id % stall_.every_n_parcels == 0) {
            std::this_thread::sleep_for(stall_.duration);
        }

        probe_->on_decide(parcel, Clock::now());
        probe_->on_act(parcel, Clock::now());
    }

private:
    static constexpr int decode_iterations = 500;
    static constexpr std::uint64_t hash_multiplier = 2654435761ULL;  // Knuth's multiplicative hash
    static constexpr unsigned int hash_shift = 13;
    static constexpr int chute_count = 12;
    static constexpr int oversize_chute = 12;  // a 13th, dedicated chute
    static constexpr double volumetric_divisor_cm3_per_kg = 5000.0;
    static constexpr double oversize_margin = 1.5;  // volumetric weight > 1.5x actual weight

    std::shared_ptr<line_simulator::ScanToDivertProbe> probe_;
    StallSchedule stall_;
};

}  // namespace ch01
