#include "ch07/lru_cache.hpp"

#include <gtest/gtest.h>

namespace ch07 {
namespace {

// A loader that returns the key itself, so a test can tell "was this a
// fresh load" from "was this the cached value" just by comparing to key.
int identity_loader(int key) { return key; }

TEST(LruCacheTest, FirstAccessOfEveryKeyIsAMiss) {
    LruCache cache(3);
    EXPECT_FALSE(cache.get_or_insert(1, identity_loader).hit);
    EXPECT_FALSE(cache.get_or_insert(2, identity_loader).hit);
}

TEST(LruCacheTest, RepeatedAccessIsAHitWithTheSameValue) {
    LruCache cache(3);
    cache.get_or_insert(1, identity_loader);
    const CacheResult second = cache.get_or_insert(1, identity_loader);
    EXPECT_TRUE(second.hit);
    EXPECT_EQ(second.value, 1);
}

// Capacity 3: insert A, B, C, then touch A (making B the least recently
// used), then insert D. LRU must evict B, the one entry nobody touched
// since it was inserted, and keep A, C, D.
TEST(LruCacheTest, EvictsTheLeastRecentlyUsedEntry) {
    LruCache cache(3);
    cache.get_or_insert(1, identity_loader);  // A
    cache.get_or_insert(2, identity_loader);  // B
    cache.get_or_insert(3, identity_loader);  // C
    cache.get_or_insert(1, identity_loader);  // touch A: B is now the LRU entry
    cache.get_or_insert(4, identity_loader);  // D: evicts B

    // Check every expected HIT before the one expected MISS: probing a miss
    // via get_or_insert necessarily inserts and may itself evict something,
    // so it must be the last probe, not one in the middle of the checks.
    EXPECT_TRUE(cache.get_or_insert(1, identity_loader).hit);   // A survives
    EXPECT_TRUE(cache.get_or_insert(3, identity_loader).hit);   // C survives
    EXPECT_TRUE(cache.get_or_insert(4, identity_loader).hit);   // D survives
    EXPECT_FALSE(cache.get_or_insert(2, identity_loader).hit);  // B was evicted
}

}  // namespace
}  // namespace ch07
