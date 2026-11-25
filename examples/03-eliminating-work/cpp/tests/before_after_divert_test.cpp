#include "ch03/after_scan_stage.hpp"
#include "ch03/before_scan_stage.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include "line_simulator/parcel.hpp"
#include "line_simulator/stage.hpp"

namespace ch03 {
namespace {

using line_simulator::Parcel;
using line_simulator::Stage;

Parcel parcel_for_barcode(std::uint64_t barcode) {
    return Parcel{
        .id = 1,
        .barcode = barcode,
        .weight_kg = 5.0,
        .length_cm = 30.0,
        .width_cm = 20.0,
        .height_cm = 10.0,
        .chute = -1,
        .scheduled_arrival = {},
    };
}

// The chapter's central correctness claim: eliminating the allocation, the
// map lookup, and the recomputed table never changes the divert decision.
// Both stages take a null probe here; neither one dereferences it unless a
// probe is actually set (see BeforeScanStage::operator() and
// AfterScanStage::operator()).
TEST(BeforeAndAfterScanStage, AgreeOnTheDivertDecisionAcrossManyBarcodes) {
    const BeforeScanStage before{nullptr};
    const AfterScanStage after{nullptr};

    constexpr int sample_count = 2000;
    std::uint64_t barcode = 1'000'000'007ULL;
    for (int i = 0; i < sample_count; ++i) {
        // A deterministic pseudo-random walk over the barcode space, not a
        // real RNG: the test only needs many distinct, reproducible
        // barcodes, not statistically uniform ones.
        barcode = (barcode * 1'000'003ULL) + 17ULL;

        Parcel before_parcel = parcel_for_barcode(barcode);
        Parcel after_parcel = parcel_for_barcode(barcode);
        before(before_parcel, Stage::Scan);
        after(after_parcel, Stage::Scan);

        ASSERT_EQ(before_parcel.chute, after_parcel.chute) << "diverged at barcode " << barcode;
    }
}

TEST(BeforeAndAfterScanStage, AgreeOnTheOversizeOverrideToo) {
    const BeforeScanStage before{nullptr};
    const AfterScanStage after{nullptr};

    // Light weight, large dimensions: volumetric weight should dominate
    // actual weight by a wide margin for every chute's zone multiplier, so
    // this parcel should trip the oversize-for-weight override in both
    // lanes, for every barcode tried.
    for (std::uint64_t barcode : {1ULL, 777ULL, 4242424242ULL, 9'999'999'999ULL}) {
        Parcel before_parcel = parcel_for_barcode(barcode);
        before_parcel.weight_kg = 0.1;
        before_parcel.length_cm = 80.0;
        before_parcel.width_cm = 80.0;
        before_parcel.height_cm = 80.0;
        Parcel after_parcel = before_parcel;

        before(before_parcel, Stage::Scan);
        after(after_parcel, Stage::Scan);

        EXPECT_EQ(before_parcel.chute, after_parcel.chute);
        EXPECT_EQ(before_parcel.chute, oversize_chute);
    }
}

TEST(BeforeAndAfterScanStage, AssignAValidChuteOrTheOversizeChute) {
    const BeforeScanStage before{nullptr};
    const AfterScanStage after{nullptr};

    Parcel before_parcel = parcel_for_barcode(4242424242ULL);
    Parcel after_parcel = parcel_for_barcode(4242424242ULL);
    before(before_parcel, Stage::Scan);
    after(after_parcel, Stage::Scan);

    EXPECT_GE(before_parcel.chute, 0);
    EXPECT_LE(before_parcel.chute, oversize_chute);
    EXPECT_GE(after_parcel.chute, 0);
    EXPECT_LE(after_parcel.chute, oversize_chute);
}

TEST(BeforeAndAfterScanStage, IgnoreNonScanStages) {
    const BeforeScanStage before{nullptr};
    const AfterScanStage after{nullptr};

    Parcel before_parcel = parcel_for_barcode(123ULL);
    Parcel after_parcel = parcel_for_barcode(123ULL);
    before(before_parcel, Stage::Checkweigh);
    after(after_parcel, Stage::Label);

    EXPECT_EQ(before_parcel.chute, -1);  // untouched
    EXPECT_EQ(after_parcel.chute, -1);   // untouched
}

}  // namespace
}  // namespace ch03
