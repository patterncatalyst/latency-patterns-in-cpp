#include "ch02/pipeline_timing.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "line_simulator/line_simulator.hpp"
#include "line_simulator/stage.hpp"

namespace ch02 {
namespace {

using line_simulator::LineConfig;
using line_simulator::LineSimulator;
using line_simulator::Parcel;
using line_simulator::Stage;

// Trivial, side-effect-free workloads: this test is about the harness's own
// bookkeeping (does every stage get a sample, does the sum add up), not
// about any particular stage's cost, so every stage does as little as
// possible.
StageWorkloads no_op_workloads() {
    return StageWorkloads{
        .checkweigh = [](Parcel& /*parcel*/) {},
        .dimension = [](Parcel& /*parcel*/) {},
        .scan = [](Parcel& parcel) { parcel.chute = 0; },
        .label = [](Parcel& /*parcel*/) {},
        .sort = [](Parcel& /*parcel*/) {},
    };
}

TEST(PipelineTimingHarness, RecordsOneSamplePerStagePerParcel) {
    LineConfig config{};
    config.parcel_count = 5;
    LineSimulator sim{config};

    PipelineTimingHarness harness;
    harness.install(sim, no_op_workloads());
    sim.run();

    EXPECT_EQ(harness.stage_samples(Stage::Checkweigh).size(), 5U);
    EXPECT_EQ(harness.stage_samples(Stage::Dimension).size(), 5U);
    EXPECT_EQ(harness.stage_samples(Stage::Scan).size(), 5U);
    EXPECT_EQ(harness.stage_samples(Stage::Label).size(), 5U);
    EXPECT_EQ(harness.stage_samples(Stage::Sort).size(), 5U);
    EXPECT_EQ(harness.scan_to_divert_samples().size(), 5U);
}

TEST(PipelineTimingHarness, ScanToDivertIsExactlyScanPlusLabelPlusSort) {
    LineConfig config{};
    config.parcel_count = 10;
    LineSimulator sim{config};

    PipelineTimingHarness harness;
    harness.install(sim, no_op_workloads());
    sim.run();

    const std::vector<Sample>& scan = harness.stage_samples(Stage::Scan);
    const std::vector<Sample>& label = harness.stage_samples(Stage::Label);
    const std::vector<Sample>& sort = harness.stage_samples(Stage::Sort);
    const std::vector<Sample>& total = harness.scan_to_divert_samples();

    ASSERT_EQ(total.size(), scan.size());
    for (std::size_t i = 0; i < total.size(); ++i) {
        const Sample expected = scan[i] + label[i] + sort[i];
        EXPECT_DOUBLE_EQ(total[i].count(), expected.count());
    }
}

TEST(PipelineTimingHarness, CheckweighAndDimensionAreExcludedFromScanToDivert) {
    // A checkweigh or dimension stage that is deliberately expensive must
    // not move scan_to_divert_samples() at all: those two stages finish
    // before the parcel reaches the scanner (see this type's class
    // comment), so their cost is reported on its own lane, never folded
    // into the scan-to-divert sum.
    LineConfig cheap_config{};
    cheap_config.parcel_count = 3;
    LineSimulator cheap_sim{cheap_config};
    PipelineTimingHarness cheap_harness;
    cheap_harness.install(cheap_sim, no_op_workloads());
    cheap_sim.run();

    LineConfig expensive_config{};
    expensive_config.parcel_count = 3;
    LineSimulator expensive_sim{expensive_config};
    PipelineTimingHarness expensive_harness;
    StageWorkloads expensive_workloads = no_op_workloads();
    expensive_workloads.checkweigh = [](Parcel& /*parcel*/) {
        volatile std::uint64_t spin = 0;
        for (int i = 0; i < 200000; ++i) {
            spin += static_cast<std::uint64_t>(i);
        }
    };
    expensive_harness.install(expensive_sim, std::move(expensive_workloads));
    expensive_sim.run();

    const Sample cheap_total = cheap_harness.scan_to_divert_samples().front();
    const Sample expensive_total = expensive_harness.scan_to_divert_samples().front();
    // The expensive checkweigh workload must not have leaked into
    // scan_to_divert: both totals should be on the same (sub-millisecond)
    // order of magnitude, not off by the checkweigh spin loop's cost.
    EXPECT_LT(expensive_total.count(), 0.001);
    EXPECT_LT(cheap_total.count(), 0.001);
}

}  // namespace
}  // namespace ch02
