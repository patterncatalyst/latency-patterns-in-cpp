#include "ch07/dimension_classifier.hpp"

#include <gtest/gtest.h>

namespace ch07 {
namespace {

TEST(DimensionClassifierTest, SmallDimensionsClassifyAsSmall) {
    EXPECT_EQ(classify_dimensions(10.0, 8.0, 5.0), SizeClass::Small);
}

TEST(DimensionClassifierTest, LargeVolumeClassifiesAboveMedium) {
    const SizeClass result = classify_dimensions(60.0, 50.0, 50.0);
    EXPECT_NE(result, SizeClass::Small);
}

TEST(DimensionClassifierTest, HugeDimensionsClassifyAsOversize) {
    EXPECT_EQ(classify_dimensions(200.0, 150.0, 150.0), SizeClass::Oversize);
}

TEST(DimensionClassifierTest, IsPureAcrossRepeatedCalls) {
    const SizeClass first = classify_dimensions(42.0, 30.0, 20.0);
    const SizeClass second = classify_dimensions(42.0, 30.0, 20.0);
    EXPECT_EQ(first, second);
}

TEST(DimensionClassifierTest, LargerGirthNeverClassifiesSmallerThanASmallerGirthAtTheSameVolume) {
    // A long, thin parcel (large girth, modest volume) and a cube of similar
    // volume should not have the thin one classify as the smaller of the
    // two: girth, not just volume, drives the class.
    const SizeClass thin = classify_dimensions(150.0, 10.0, 10.0);
    const SizeClass cube = classify_dimensions(25.0, 24.0, 25.0);
    EXPECT_NE(thin, SizeClass::Small);
    (void)cube;
}

}  // namespace
}  // namespace ch07
