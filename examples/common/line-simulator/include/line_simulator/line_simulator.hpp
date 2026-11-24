#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "line_simulator/parcel.hpp"
#include "line_simulator/scan_to_divert_probe.hpp"
#include "line_simulator/stage.hpp"

namespace line_simulator {

struct LineConfig {
    double belt_speed_m_per_s = 2.0;           // conveyor_speed
    double scan_to_diverter_distance_m = 1.5;  // distance(scan, diverter)
    double parcels_per_second = 10.0;          // arrival rate
    std::uint64_t parcel_count = 1000;         // how many parcels to run
    std::uint64_t random_seed = 1;             // deterministic by default
};

// The one shared running example for every chapter: a parcel sortation line
// past checkweigh, dimension, scan, label, sort (see CLAUDE.md, "Domain").
// Sub-techniques are measured lanes a chapter installs via set_stage_work,
// not separate example projects.
//
// run() drives every parcel through at the configured rate regardless of how
// long a stage's work takes: the belt never waits for a slow consumer, so
// coordinated omission is physical here, not a definition to memorize (see
// CLAUDE.md, "Domain"). Each parcel's `scheduled_arrival` is fixed at
// creation from the configured rate; it is never pushed back to match
// however long processing actually took.
class LineSimulator {
public:
    explicit LineSimulator(LineConfig config);

    // Must be set before run() for the scan stage's default work to record
    // the scan-to-divert spine metric. A chapter that overrides the scan
    // stage via set_stage_work is responsible for calling the probe itself.
    void set_scan_to_divert_probe(std::shared_ptr<ScanToDivertProbe> probe);

    // Per-stage work hook: a chapter overrides one stage's workload (e.g.
    // Ch3's decode path) without touching the others. Passing an empty
    // std::function restores that stage's default work.
    using StageWork = std::function<void(Parcel&, Stage)>;
    void set_stage_work(Stage stage, StageWork work);

    // Runs config.parcel_count parcels through the line synchronously on the
    // calling thread. Returns the number of parcels that completed sort.
    std::uint64_t run();

    // The physical scan-to-divert deadline this config implies:
    // distance / belt_speed (see CLAUDE.md, "The spine").
    [[nodiscard]] std::chrono::duration<double> scan_to_divert_deadline() const;

    [[nodiscard]] const LineConfig& config() const noexcept { return config_; }

private:
    void run_stage(Parcel& parcel, Stage stage);
    void default_stage_work(Parcel& parcel, Stage stage);

    LineConfig config_;
    std::shared_ptr<ScanToDivertProbe> probe_;
    std::array<StageWork, stage_count> stage_work_{};
};

}  // namespace line_simulator
