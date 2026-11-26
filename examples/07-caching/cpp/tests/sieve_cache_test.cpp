#include "ch07/sieve_cache.hpp"

#include <gtest/gtest.h>

namespace ch07 {
namespace {

int identity_loader(int key) { return key; }

TEST(SieveCacheTest, FirstAccessOfEveryKeyIsAMiss) {
    SieveCache cache(3);
    EXPECT_FALSE(cache.get_or_insert(1, identity_loader).hit);
}

TEST(SieveCacheTest, RepeatedAccessIsAHit) {
    SieveCache cache(3);
    cache.get_or_insert(1, identity_loader);
    EXPECT_TRUE(cache.get_or_insert(1, identity_loader).hit);
}

// Capacity 3. Insert A, B, C (queue, newest to oldest: C, B, A; none
// visited). Access B, marking it visited. Insert D: the hand starts at the
// back (A, never visited) and evicts it immediately; the hand is left at B.
// Access C, marking it visited. Insert E: the hand's scan hits B (visited,
// clear the bit and move on), then C (visited from the access just above,
// clear the bit and move on), then D (never accessed, unvisited) and evicts
// D. The entries that got a "second chance" by being accessed once, B and
// C, both survive; A (never accessed) and D (accessed zero times before the
// very next eviction reached it) do not. This is the behavior that
// distinguishes SIEVE from plain FIFO: a single access buys an entry one
// extra sweep of the hand, but a brand-new entry gets no such protection
// until it has been through at least one access itself.
TEST(SieveCacheTest, SecondChanceProtectsAccessedEntriesOverNewInsertions) {
    SieveCache cache(3);
    cache.get_or_insert(1, identity_loader);  // A
    cache.get_or_insert(2, identity_loader);  // B
    cache.get_or_insert(3, identity_loader);  // C
    cache.get_or_insert(2, identity_loader);  // touch B: visited
    cache.get_or_insert(4, identity_loader);  // D: evicts A (unvisited, at the hand)
    cache.get_or_insert(3, identity_loader);  // touch C: visited
    cache.get_or_insert(5, identity_loader);  // E: clears B and C's bits, evicts D

    // Check the expected hits before the expected misses (see
    // lru_cache_test.cpp for why a miss probe, which reinserts, has to come
    // after every hit check it could otherwise disturb).
    EXPECT_TRUE(cache.get_or_insert(5, identity_loader).hit);   // E survives
    EXPECT_TRUE(cache.get_or_insert(3, identity_loader).hit);   // C survives (had its second chance)
    EXPECT_TRUE(cache.get_or_insert(2, identity_loader).hit);   // B survives (had its second chance)
    EXPECT_FALSE(cache.get_or_insert(1, identity_loader).hit);  // A was evicted
    EXPECT_FALSE(cache.get_or_insert(4, identity_loader).hit);  // D was evicted
}

}  // namespace
}  // namespace ch07
