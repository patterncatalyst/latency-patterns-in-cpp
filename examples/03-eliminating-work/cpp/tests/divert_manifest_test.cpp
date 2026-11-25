#include "ch03/divert_manifest.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <unordered_map>

namespace ch03 {
namespace {

TEST(DecodeKeyForBarcode, StaysWithinTheDenseKeySpace) {
    for (std::uint64_t barcode : {0ULL, 1ULL, 4242424242ULL, 9'999'999'999ULL}) {
        const int key = decode_key_for_barcode(barcode);
        EXPECT_GE(key, 0);
        EXPECT_LT(key, key_space);
    }
}

TEST(DecodeKeyForBarcode, IsDeterministicForTheSameBarcode) {
    EXPECT_EQ(decode_key_for_barcode(123456789ULL), decode_key_for_barcode(123456789ULL));
}

TEST(ManifestChuteForKey, StaysWithinTheChuteRangeForEveryDenseKey) {
    for (int key = 0; key < key_space; ++key) {
        const int chute = manifest_chute_for_key(key);
        EXPECT_GE(chute, 0);
        EXPECT_LT(chute, chute_count);
    }
}

TEST(ManifestZoneMultiplierForChute, StaysWithinItsDocumentedBand) {
    for (int chute = 0; chute < chute_count; ++chute) {
        const double multiplier = manifest_zone_multiplier_for_chute(chute);
        EXPECT_GE(multiplier, 0.80);
        EXPECT_LT(multiplier, 1.20);
    }
}

TEST(BuildManifestMapAndTable, AgreeWithEachOtherForEveryDenseKey) {
    const std::unordered_map<int, int> map = build_manifest_map();
    const std::array<int, key_space> table = build_manifest_table();

    ASSERT_EQ(map.size(), static_cast<std::size_t>(key_space));
    for (int key = 0; key < key_space; ++key) {
        EXPECT_EQ(map.at(key), table[static_cast<std::size_t>(key)])
            << "map and table disagree at dense key " << key;
    }
}

}  // namespace
}  // namespace ch03
