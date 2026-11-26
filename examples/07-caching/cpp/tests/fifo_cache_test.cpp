#include "ch07/fifo_cache.hpp"

#include <gtest/gtest.h>

namespace ch07 {
namespace {

int identity_loader(int key) { return key; }

TEST(FifoCacheTest, FirstAccessOfEveryKeyIsAMiss) {
    FifoCache cache(3);
    EXPECT_FALSE(cache.get_or_insert(1, identity_loader).hit);
}

TEST(FifoCacheTest, RepeatedAccessIsAHit) {
    FifoCache cache(3);
    cache.get_or_insert(1, identity_loader);
    EXPECT_TRUE(cache.get_or_insert(1, identity_loader).hit);
}

// Capacity 3: insert A, B, C, then touch A repeatedly (FIFO must ignore
// this: access never changes eviction order), then insert D. FIFO evicts A,
// the oldest INSERTION, even though it is the most recently ACCESSED entry,
// which is exactly the behavior that tells FIFO apart from LRU on this
// trace.
TEST(FifoCacheTest, EvictsTheOldestInsertionRegardlessOfAccess) {
    FifoCache cache(3);
    cache.get_or_insert(1, identity_loader);  // A
    cache.get_or_insert(2, identity_loader);  // B
    cache.get_or_insert(3, identity_loader);  // C
    cache.get_or_insert(1, identity_loader);  // touch A: a no-op for FIFO's order
    cache.get_or_insert(1, identity_loader);  // touch A again
    cache.get_or_insert(4, identity_loader);  // D: evicts A, the oldest insertion

    // Check the expected hits before the expected miss (see lru_cache_test.cpp
    // for why the miss probe has to come last).
    EXPECT_TRUE(cache.get_or_insert(2, identity_loader).hit);   // B survives
    EXPECT_TRUE(cache.get_or_insert(3, identity_loader).hit);   // C survives
    EXPECT_TRUE(cache.get_or_insert(4, identity_loader).hit);   // D survives
    EXPECT_FALSE(cache.get_or_insert(1, identity_loader).hit);  // A was evicted despite being accessed
}

}  // namespace
}  // namespace ch07
