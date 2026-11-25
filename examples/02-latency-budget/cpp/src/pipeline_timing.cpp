#include "ch02/pipeline_timing.hpp"

namespace ch02 {

void PipelineTimingHarness::install(line_simulator::LineSimulator& sim, StageWorkloads workloads) {
    using line_simulator::Parcel;
    using line_simulator::Stage;

    // Five explicit registrations rather than a loop over stage_order plus a
    // switch: each one moves exactly one StageWorkloads member into exactly
    // one lambda, which reads directly as "this stage does this work" and
    // leaves no shared mutable state between registrations.
    sim.set_stage_work(Stage::Checkweigh,
                        [this, work = std::move(workloads.checkweigh)](Parcel& parcel, Stage stage) {
                            time_stage(parcel, stage, work);
                        });
    sim.set_stage_work(Stage::Dimension,
                        [this, work = std::move(workloads.dimension)](Parcel& parcel, Stage stage) {
                            time_stage(parcel, stage, work);
                        });
    sim.set_stage_work(Stage::Scan,
                        [this, work = std::move(workloads.scan)](Parcel& parcel, Stage stage) {
                            time_stage(parcel, stage, work);
                        });
    sim.set_stage_work(Stage::Label,
                        [this, work = std::move(workloads.label)](Parcel& parcel, Stage stage) {
                            time_stage(parcel, stage, work);
                        });
    sim.set_stage_work(Stage::Sort,
                        [this, work = std::move(workloads.sort)](Parcel& parcel, Stage stage) {
                            time_stage(parcel, stage, work);
                        });
}

void PipelineTimingHarness::time_stage(line_simulator::Parcel& parcel, line_simulator::Stage stage,
                                        const std::function<void(line_simulator::Parcel&)>& work) {
    using line_simulator::Clock;
    using line_simulator::Stage;

    const auto start = Clock::now();
    work(parcel);
    const Sample elapsed = Clock::now() - start;

    const auto index = static_cast<std::size_t>(stage);
    stage_samples_[index].push_back(elapsed);
    current_parcel_[index] = elapsed;

    if (stage == Stage::Sort) {
        // scan, label, and sort are indices 2, 3, 4 (see line_simulator/stage.hpp);
        // checkweigh and dimension finish before the parcel reaches the
        // scanner, so they are not part of this sum (see this type's class
        // comment).
        const Sample total = current_parcel_[static_cast<std::size_t>(Stage::Scan)] +
                              current_parcel_[static_cast<std::size_t>(Stage::Label)] +
                              current_parcel_[static_cast<std::size_t>(Stage::Sort)];
        scan_to_divert_samples_.push_back(total);
    }
}

}  // namespace ch02
