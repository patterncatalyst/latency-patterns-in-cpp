#pragma once

#include <chrono>
#include <cstdint>

namespace line_simulator {

using Clock = std::chrono::steady_clock;

// One parcel moving down the line. Fields fill in as the parcel is created;
// `chute` starts unset (-1) and is written by the scan stage's routing
// decision (see ScanToDivertProbe and LineSimulator::set_stage_work).
struct Parcel {
    std::uint64_t id = 0;
    std::uint64_t barcode = 0;
    double weight_kg = 0.0;
    double length_cm = 0.0;
    double width_cm = 0.0;
    double height_cm = 0.0;
    int chute = -1;

    // The belt's fixed schedule for this parcel, computed from the
    // configured arrival rate at creation time. The belt never waits for a
    // slow stage (see CLAUDE.md, "Domain"): this timestamp is set once and
    // never adjusted to how long processing actually takes, so downstream
    // measurement code (examples/_lib) can tell a late arrival from a slow
    // stage.
    Clock::time_point scheduled_arrival;
};

}  // namespace line_simulator
