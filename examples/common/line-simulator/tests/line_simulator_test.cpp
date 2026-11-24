#include "line_simulator/line_simulator.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "line_simulator/parcel.hpp"
#include "line_simulator/scan_to_divert_probe.hpp"
#include "line_simulator/stage.hpp"

namespace line_simulator {
namespace {

using ::testing::_;
using ::testing::InSequence;

class MockScanToDivertProbe : public ScanToDivertProbe {
public:
    MOCK_METHOD(void, on_sense, (const Parcel&, Clock::time_point), (override));
    MOCK_METHOD(void, on_decide, (const Parcel&, Clock::time_point), (override));
    MOCK_METHOD(void, on_act, (const Parcel&, Clock::time_point), (override));
};

TEST(LineSimulator, RunsTheConfiguredParcelCount) {
    LineSimulator sim{LineConfig{.parcel_count = 25}};
    EXPECT_EQ(sim.run(), 25U);
}

TEST(LineSimulator, ScanStageSensesDecidesThenActsForEveryParcel) {
    auto probe = std::make_shared<MockScanToDivertProbe>();
    {
        InSequence seq;
        EXPECT_CALL(*probe, on_sense(_, _));
        EXPECT_CALL(*probe, on_decide(_, _));
        EXPECT_CALL(*probe, on_act(_, _));
    }

    LineSimulator sim{LineConfig{.parcel_count = 1}};
    sim.set_scan_to_divert_probe(probe);
    EXPECT_EQ(sim.run(), 1U);
}

TEST(LineSimulator, ScanToDivertDeadlineIsDistanceOverBeltSpeed) {
    LineSimulator sim{
        LineConfig{.belt_speed_m_per_s = 2.0, .scan_to_diverter_distance_m = 1.5}};
    EXPECT_NEAR(sim.scan_to_divert_deadline().count(), 0.75, 1e-9);
}

TEST(LineSimulator, DefaultScanStageAssignsAChuteFromTheBarcode) {
    std::vector<Parcel> completed;
    LineSimulator sim{LineConfig{.parcel_count = 5}};
    // Capture the final parcel state via the last stage, without overriding
    // the scan stage's default routing decision.
    sim.set_stage_work(Stage::Sort, [&completed](Parcel& parcel, Stage) {
        completed.push_back(parcel);
    });

    ASSERT_EQ(sim.run(), 5U);
    ASSERT_EQ(completed.size(), 5U);
    for (const Parcel& parcel : completed) {
        EXPECT_GE(parcel.chute, 0);
        EXPECT_LT(parcel.chute, 12);
    }
}

TEST(LineSimulator, CustomStageWorkReplacesTheDefaultForThatStageOnly) {
    int scan_calls = 0;
    LineSimulator sim{LineConfig{.parcel_count = 3}};
    sim.set_stage_work(Stage::Scan, [&scan_calls](Parcel& parcel, Stage) {
        ++scan_calls;
        parcel.chute = 7;  // a technique a later chapter would install here
    });

    std::vector<Parcel> completed;
    sim.set_stage_work(Stage::Sort,
                        [&completed](Parcel& parcel, Stage) { completed.push_back(parcel); });

    ASSERT_EQ(sim.run(), 3U);
    EXPECT_EQ(scan_calls, 3);
    for (const Parcel& parcel : completed) {
        EXPECT_EQ(parcel.chute, 7);
    }
}

}  // namespace
}  // namespace line_simulator
