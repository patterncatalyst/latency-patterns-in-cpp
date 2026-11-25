#include "ch07/lfu_cache.hpp"

#include <gtest/gtest.h>

namespace ch07 {
namespace {

int identity_loader(int key) { return key; }

TEST(LfuCacheTest, FirstAccessOfEveryKeyIsAMiss) {
    LfuCache cache(3);
    EXPECT_FALSE(cache.get_or_insert(1, identity_loader).hit);
}

TEST(LfuCacheTest, RepeatedAccessIsAHit) {
    LfuCache cache(3);
    cache.get_or_insert(1, identity_loader);
    EXPECT_TRUE(cache.get_or_insert(1, identity_loader).hit);
}

// Capacity 3: insert A, B, C (each at frequency 1), then access A twice more
// (frequency 3) and B once more (frequency 2), leaving C the only entry
// still at frequency 1. Inserting D must evict C, the least-frequently-used
// entry, even though C is not the oldest insertion (that is A) and not the
// least recently touched (that is also effectively C, but LFU's rule is
// frequency, not recency, which is what this trace is built to isolate).
TEST(LfuCacheTest, EvictsTheLeastFrequentlyUsedEntry) {
    LfuCache cache(3);
    cache.get_or_insert(1, identity_loader);  // A, freq 1
    cache.get_or_insert(2, identity_loader);  // B, freq 1
    cache.get_or_insert(3, identity_loader);  // C, freq 1
    cache.get_or_insert(1, identity_loader);  // A, freq 2
    cache.get_or_insert(1, identity_loader);  // A, freq 3
    cache.get_or_insert(2, identity_loader);  // B, freq 2
    cache.get_or_insert(4, identity_loader);  // D: evicts C, the only entry still at freq 1

    // Check the expected hits before the expected miss (see lru_cache_test.cpp
    // for why the miss probe has to come last).
    EXPECT_TRUE(cache.get_or_insert(1, identity_loader).hit);   // A survives
    EXPECT_TRUE(cache.get_or_insert(2, identity_loader).hit);   // B survives
    EXPECT_TRUE(cache.get_or_insert(4, identity_loader).hit);   // D survives
    EXPECT_FALSE(cache.get_or_insert(3, identity_loader).hit);  // C was evicted
}

}  // namespace
}  // namespace ch07
