#include "ch01/loaded_scan_stage.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include "line_simulator/parcel.hpp"
#include "line_simulator/scan_to_divert_probe.hpp"
#include "line_simulator/stage.hpp"

namespace ch01 {
namespace {

using ::testing::_;
using ::testing::InSequence;
using line_simulator::Clock;
using line_simulator::Parcel;
using line_simulator::ScanToDivertProbe;
using line_simulator::Stage;

class MockScanToDivertProbe : public ScanToDivertProbe {
public:
    MOCK_METHOD(void, on_sense, (const Parcel&, Clock::time_point), (override));
    MOCK_METHOD(void, on_decide, (const Parcel&, Clock::time_point), (override));
    MOCK_METHOD(void, on_act, (const Parcel&, Clock::time_point), (override));
};

// Builds a Parcel by field assignment rather than a partial designated
// initializer, so a test only has to name the fields it cares about without
// tripping a missing-field warning on the ones (like scheduled_arrival) it
// does not.
Parcel make_parcel(std::uint64_t id, std::uint64_t barcode, double weight_kg, double length_cm,
                    double width_cm, double height_cm) {
    Parcel parcel{};
    parcel.id = id;
    parcel.barcode = barcode;
    parcel.weight_kg = weight_kg;
    parcel.length_cm = length_cm;
    parcel.width_cm = width_cm;
    parcel.height_cm = height_cm;
    return parcel;
}

// every_n_parcels=0 and a near-zero decode cost would still leave real
// arithmetic running; tests below only assert on outcomes (probe call
// order, chute assignment), never on how fast that arithmetic ran.

TEST(LoadedScanStage, IgnoresEveryStageOtherThanScan) {
    auto probe = std::make_shared<MockScanToDivertProbe>();
    EXPECT_CALL(*probe, on_sense(_, _)).Times(0);
    EXPECT_CALL(*probe, on_decide(_, _)).Times(0);
    EXPECT_CALL(*probe, on_act(_, _)).Times(0);

    const LoadedScanStage workload{probe, StallSchedule{.every_n_parcels = 0}};
    Parcel parcel = make_parcel(1, 1234567890ULL, 5.0, 10.0, 10.0, 10.0);
    workload(parcel, Stage::Checkweigh);
    workload(parcel, Stage::Dimension);
    workload(parcel, Stage::Label);
    workload(parcel, Stage::Sort);

    EXPECT_EQ(parcel.chute, -1);  // untouched: none of those stages is Scan
}

TEST(LoadedScanStage, ScanSensesDecidesThenActsInOrder) {
    auto probe = std::make_shared<MockScanToDivertProbe>();
    {
        InSequence seq;
        EXPECT_CALL(*probe, on_sense(_, _));
        EXPECT_CALL(*probe, on_decide(_, _));
        EXPECT_CALL(*probe, on_act(_, _));
    }

    const LoadedScanStage workload{probe, StallSchedule{.every_n_parcels = 0}};
    Parcel parcel = make_parcel(1, 1234567890ULL, 5.0, 10.0, 10.0, 10.0);
    workload(parcel, Stage::Scan);
}

TEST(LoadedScanStage, AssignsAChuteInRangeForANonOversizeParcel) {
    auto probe = std::make_shared<MockScanToDivertProbe>();
    EXPECT_CALL(*probe, on_sense(_, _));
    EXPECT_CALL(*probe, on_decide(_, _));
    EXPECT_CALL(*probe, on_act(_, _));

    const LoadedScanStage workload{probe, StallSchedule{.every_n_parcels = 0}};
    // A dense, heavy parcel: volumetric weight (10*10*10/5000 = 0.2kg) is far
    // under the actual weight (5kg), so this parcel never crosses the
    // oversize-for-its-weight threshold.
    Parcel parcel = make_parcel(1, 42, 5.0, 10.0, 10.0, 10.0);
    workload(parcel, Stage::Scan);

    EXPECT_GE(parcel.chute, 0);
    EXPECT_LT(parcel.chute, 12);
}

TEST(LoadedScanStage, RoutesAnOversizeForItsWeightParcelToTheDedicatedChute) {
    auto probe = std::make_shared<MockScanToDivertProbe>();
    EXPECT_CALL(*probe, on_sense(_, _));
    EXPECT_CALL(*probe, on_decide(_, _));
    EXPECT_CALL(*probe, on_act(_, _));

    const LoadedScanStage workload{probe, StallSchedule{.every_n_parcels = 0}};
    // A large, light parcel: volumetric weight (80*80*80/5000 = 102.4kg) is
    // far over 1.5x the actual weight (0.5kg), so this one is oversize for
    // its weight.
    Parcel parcel = make_parcel(1, 42, 0.5, 80.0, 80.0, 80.0);
    workload(parcel, Stage::Scan);

    EXPECT_EQ(parcel.chute, 12);  // the dedicated oversize chute
}

TEST(LoadedScanStage, StallsOnlyOnTheConfiguredSchedule) {
    auto probe = std::make_shared<MockScanToDivertProbe>();
    EXPECT_CALL(*probe, on_sense(_, _)).Times(2);
    EXPECT_CALL(*probe, on_decide(_, _)).Times(2);
    EXPECT_CALL(*probe, on_act(_, _)).Times(2);

    // A 5ms stall every 2nd parcel, kept short so the test stays fast.
    const LoadedScanStage workload{probe,
                                    StallSchedule{.every_n_parcels = 2, .duration = std::chrono::milliseconds(5)}};
    Parcel fast_parcel = make_parcel(1, 42, 5.0, 10.0, 10.0, 10.0);
    Parcel stalled_parcel = make_parcel(2, 42, 5.0, 10.0, 10.0, 10.0);

    const auto fast_start = Clock::now();
    workload(fast_parcel, Stage::Scan);
    const auto fast_elapsed = Clock::now() - fast_start;

    const auto stalled_start = Clock::now();
    workload(stalled_parcel, Stage::Scan);
    const auto stalled_elapsed = Clock::now() - stalled_start;

    EXPECT_LT(fast_elapsed, std::chrono::milliseconds(5));
    EXPECT_GE(stalled_elapsed, std::chrono::milliseconds(5));
}

}  // namespace
}  // namespace ch01
