#include "ch09/parcel.hpp"

#include <gtest/gtest.h>

namespace ch09 {
namespace {

TEST(ParcelTest, DeterministicForTheSameSeed) {
    const auto a = generate_parcels(500, 64, 42);
    const auto b = generate_parcels(500, 64, 42);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].parcel_id, b[i].parcel_id);
        EXPECT_EQ(a[i].chute_id, b[i].chute_id);
    }
}

TEST(ParcelTest, EveryChuteIdInRange) {
    const auto parcels = generate_parcels(2000, 16, 7);
    for (const Parcel& parcel : parcels) {
        EXPECT_LT(parcel.chute_id, 16U);
    }
}

TEST(ParcelTest, ParcelIdsAreSequential) {
    const auto parcels = generate_parcels(10, 4, 1);
    for (std::size_t i = 0; i < parcels.size(); ++i) {
        EXPECT_EQ(parcels[i].parcel_id, i);
    }
}

}  // namespace
}  // namespace ch09
