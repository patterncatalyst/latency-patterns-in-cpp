#include "ch04/layout_scan_stage.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include "line_simulator/parcel.hpp"
#include "line_simulator/stage.hpp"

namespace ch04 {
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

// The chapter's central correctness claim: the array-of-pointers ledger and
// the struct-of-arrays ledger reach the identical divert decision for every
// barcode either one sees. Both stages take a null probe here; neither one
// dereferences it unless a probe is actually set (see
// BeforeLayoutScanStage::operator() and AfterLayoutScanStage::operator()).
TEST(BeforeAndAfterLayoutScanStage, AgreeOnTheDivertDecisionAcrossManyBarcodes) {
    BeforeLayoutScanStage before{nullptr};
    AfterLayoutScanStage after{nullptr};

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
    EXPECT_EQ(before.review_count(), after.review_count());
}

TEST(BeforeAndAfterLayoutScanStage, FlagsARepeatedScanKeyForReviewInBothLanes) {
    BeforeLayoutScanStage before{nullptr};
    AfterLayoutScanStage after{nullptr};

    Parcel before_first = parcel_for_barcode(5'555'555'555ULL);
    Parcel after_first = parcel_for_barcode(5'555'555'555ULL);
    before(before_first, Stage::Scan);
    after(after_first, Stage::Scan);
    ASSERT_NE(before_first.chute, review_chute);
    ASSERT_NE(after_first.chute, review_chute);

    // Same barcode again, immediately after, so it is still well inside the
    // ledger window: both lanes must flag the repeat for manual review.
    Parcel before_repeat = parcel_for_barcode(5'555'555'555ULL);
    Parcel after_repeat = parcel_for_barcode(5'555'555'555ULL);
    before(before_repeat, Stage::Scan);
    after(after_repeat, Stage::Scan);

    EXPECT_EQ(before_repeat.chute, review_chute);
    EXPECT_EQ(after_repeat.chute, review_chute);
}

TEST(BeforeAndAfterLayoutScanStage, IgnoreNonScanStages) {
    BeforeLayoutScanStage before{nullptr};
    AfterLayoutScanStage after{nullptr};

    Parcel before_parcel = parcel_for_barcode(123ULL);
    Parcel after_parcel = parcel_for_barcode(123ULL);
    before(before_parcel, Stage::Checkweigh);
    after(after_parcel, Stage::Label);

    EXPECT_EQ(before_parcel.chute, -1);  // untouched
    EXPECT_EQ(after_parcel.chute, -1);   // untouched
}

}  // namespace
}  // namespace ch04
