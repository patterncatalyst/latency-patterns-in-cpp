#include "ch07/ttl_lru_cache.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace ch07 {
namespace {

int identity_loader(int key) { return key; }

TEST(TtlLruCacheTest, EntryIsAHitWithinItsTtlWindow) {
    TtlLruCache cache(2, std::chrono::milliseconds(10));
    const auto t0 = std::chrono::steady_clock::now();
    cache.get_or_insert(1, identity_loader, t0);  // miss, inserted_at = t0
    const CacheResult result = cache.get_or_insert(1, identity_loader, t0 + std::chrono::milliseconds(5));
    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.value, 1);
}

TEST(TtlLruCacheTest, EntryExpiresAndIsReloadedPastItsTtlWindow) {
    TtlLruCache cache(2, std::chrono::milliseconds(10));
    const auto t0 = std::chrono::steady_clock::now();
    cache.get_or_insert(1, identity_loader, t0);  // miss, inserted_at = t0
    const CacheResult result = cache.get_or_insert(1, identity_loader, t0 + std::chrono::milliseconds(15));
    EXPECT_FALSE(result.hit);  // past the TTL: treated as if it had never been cached
    EXPECT_EQ(result.value, 1);
}

TEST(TtlLruCacheTest, ReloadingAfterExpiryResetsTheTtlWindow) {
    TtlLruCache cache(2, std::chrono::milliseconds(10));
    const auto t0 = std::chrono::steady_clock::now();
    cache.get_or_insert(1, identity_loader, t0);
    cache.get_or_insert(1, identity_loader, t0 + std::chrono::milliseconds(15));  // expires, reloads
    const CacheResult result = cache.get_or_insert(1, identity_loader, t0 + std::chrono::milliseconds(20));
    EXPECT_TRUE(result.hit);  // only 5ms since the reload at t0+15ms, well inside the 10ms window
}

TEST(TtlLruCacheTest, StillEvictsByLeastRecentlyUsedWithinTheTtlWindow) {
    TtlLruCache cache(2, std::chrono::milliseconds(10));
    const auto t0 = std::chrono::steady_clock::now();
    cache.get_or_insert(1, identity_loader, t0);  // A
    cache.get_or_insert(2, identity_loader, t0);  // B
    cache.get_or_insert(3, identity_loader, t0);  // C: evicts A (LRU), all well inside the TTL window

    const auto t1 = t0 + std::chrono::milliseconds(1);
    EXPECT_TRUE(cache.get_or_insert(2, identity_loader, t1).hit);   // B survives
    EXPECT_TRUE(cache.get_or_insert(3, identity_loader, t1).hit);   // C survives
    EXPECT_FALSE(cache.get_or_insert(1, identity_loader, t1).hit);  // A was evicted by capacity, not TTL
}

}  // namespace
}  // namespace ch07
