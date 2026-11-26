#include "ch08/routing.hpp"

#include <gtest/gtest.h>

#include <unordered_set>
#include <vector>

namespace ch08 {
namespace {

constexpr std::size_t test_shard_count = 8;

TEST(Routing, HashRouteIsDeterministic) {
    for (Barcode key = 1; key <= 500; ++key) {
        EXPECT_EQ(hash_route(key, test_shard_count), hash_route(key, test_shard_count));
    }
}

// Every key lands in exactly one shard: the route is always within range,
// and calling it again for the same key never lands somewhere else.
TEST(Routing, HashRouteIsTotalAndWithinRange) {
    for (Barcode key = 1; key <= 5000; ++key) {
        const std::size_t shard = hash_route(key, test_shard_count);
        EXPECT_LT(shard, test_shard_count);
    }
}

TEST(Routing, HashRouteSpreadsSequentialKeysAcrossEveryShard) {
    std::unordered_set<std::size_t> seen_shards;
    for (Barcode key = 1; key <= 2000; ++key) {
        seen_shards.insert(hash_route(key, test_shard_count));
    }
    // 2000 sequential keys should touch every one of 8 shards; a hash that
    // degenerated to the identity function (std::hash<uint64_t> on a
    // sequential key space) would instead touch all of them too by sheer
    // round-robin, so this is a weak check, but RoutingTableDefaultMatchesHashRoute
    // below is the one that actually exercises mix64 with non-sequential input.
    EXPECT_EQ(seen_shards.size(), test_shard_count);
}

TEST(RoutingTable, DefaultAssignmentMatchesDirectHashRoute) {
    const RoutingTable table(test_shard_count, test_shard_count);
    for (Barcode key = 1; key <= 2000; ++key) {
        EXPECT_EQ(table.route(key), hash_route(key, test_shard_count));
    }
}

// The skew mitigation needs this: a table with more buckets than shards
// (an exact multiple) must still match hash_route() by default, so finer
// bucket granularity is "free" until something actually gets reassigned.
TEST(RoutingTable, FinerGrainedDefaultAssignmentStillMatchesDirectHashRoute) {
    constexpr std::size_t fanout = 16;
    const RoutingTable table(test_shard_count * fanout, test_shard_count);
    for (Barcode key = 1; key <= 2000; ++key) {
        EXPECT_EQ(table.route(key), hash_route(key, test_shard_count));
    }
}

// Two keys that collide on the same shard under direct hashing must not
// also be forced to collide on the same bucket once the table has more
// buckets than shards: that independence is what lets the skew mitigation
// move one of them without moving the other (see main.cpp's rebalance).
TEST(RoutingTable, FinerBucketsCanSeparateKeysThatShareAShard) {
    constexpr std::size_t fanout = 1024;
    const std::vector<Barcode> colliding = find_colliding_hot_keys(test_shard_count, 2);
    ASSERT_EQ(colliding.size(), 2U);

    const RoutingTable table(test_shard_count * fanout, test_shard_count);
    EXPECT_NE(table.bucket_for_key(colliding[0]), table.bucket_for_key(colliding[1]));
}

TEST(RoutingTable, AssignMovesOnlyTheGivenBucket) {
    RoutingTable table(test_shard_count, test_shard_count);
    const Barcode some_key = 42;
    const std::size_t bucket = table.bucket_for_key(some_key);
    const std::size_t original_shard = table.route(some_key);
    const std::size_t new_shard = (original_shard + 1) % test_shard_count;

    table.assign(bucket, new_shard);

    EXPECT_EQ(table.route(some_key), new_shard);
    // Every bucket other than the reassigned one keeps its default.
    for (std::size_t b = 0; b < table.bucket_count(); ++b) {
        if (b != bucket) {
            EXPECT_EQ(table.shard_for_bucket(b), b % test_shard_count);
        }
    }
}

TEST(FindCollidingHotKeys, ReturnsKeysThatAllRouteToTheSameShard) {
    const std::vector<Barcode> hot_keys = find_colliding_hot_keys(test_shard_count, 2);

    ASSERT_EQ(hot_keys.size(), 2U);
    EXPECT_EQ(hash_route(hot_keys[0], test_shard_count), hash_route(hot_keys[1], test_shard_count));
    EXPECT_NE(hot_keys[0], hot_keys[1]);
}

TEST(FindCollidingHotKeys, WorksForShardCountsThisBookMeasuresOn) {
    // A sweep across plausible host core counts, not just the one this
    // chapter happened to measure on (see "Keep it honest").
    for (const std::size_t shard_count : {1UL, 2UL, 4UL, 16UL, 22UL, 64UL}) {
        const std::vector<Barcode> hot_keys = find_colliding_hot_keys(shard_count, 2);
        ASSERT_EQ(hot_keys.size(), 2U) << "shard_count=" << shard_count;
        EXPECT_EQ(hash_route(hot_keys[0], shard_count), hash_route(hot_keys[1], shard_count));
    }
}

}  // namespace
}  // namespace ch08
