#include "ch08/sharded_tracker_pool.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace ch08 {
namespace {

TEST(ShardedTrackerPool, SubmitThenWaitDrainedAppliesEveryObservation) {
    ShardedTrackerPool pool(4);
    double completion_us = -1.0;

    pool.submit(0, 42, &completion_us);
    pool.wait_drained();

    EXPECT_GE(completion_us, 0.0);
    EXPECT_EQ(pool.snapshot(42, 0).seen_count, 1U);
}

TEST(ShardedTrackerPool, EachShardIsTouchedOnlyByTheKeysRoutedToIt) {
    ShardedTrackerPool pool(4);

    pool.submit(0, 1, nullptr);
    pool.submit(1, 2, nullptr);
    pool.submit(2, 3, nullptr);
    pool.wait_drained();

    EXPECT_EQ(pool.shard_size(0), 1U);
    EXPECT_EQ(pool.shard_size(1), 1U);
    EXPECT_EQ(pool.shard_size(2), 1U);
    EXPECT_EQ(pool.shard_size(3), 0U);
    // Key 1 was only ever submitted to shard 0; it must not show up in any
    // other shard's map.
    EXPECT_EQ(pool.snapshot(1, 1).seen_count, 0U);
    EXPECT_EQ(pool.snapshot(1, 0).seen_count, 1U);
}

TEST(ShardedTrackerPool, RepeatedSubmitsToOneShardAccumulate) {
    ShardedTrackerPool pool(2);
    std::vector<double> completion_us(100, -1.0);

    for (int i = 0; i < 100; ++i) {
        pool.submit(1, 7, &completion_us[static_cast<std::size_t>(i)]);
    }
    pool.wait_drained();

    EXPECT_EQ(pool.snapshot(7, 1).seen_count, 100U);
    EXPECT_EQ(pool.shard_processed_count(1), 100U);
    EXPECT_EQ(pool.shard_processed_count(0), 0U);
    for (const double us : completion_us) {
        EXPECT_GE(us, 0.0);
    }
}

TEST(ShardedTrackerPool, ManyShardsManyKeysAllLand) {
    constexpr std::size_t shard_count = 8;
    constexpr int keys_per_shard = 500;
    ShardedTrackerPool pool(shard_count);

    for (std::size_t s = 0; s < shard_count; ++s) {
        for (int k = 0; k < keys_per_shard; ++k) {
            pool.submit(s, static_cast<Barcode>((s * 1000) + static_cast<std::size_t>(k)), nullptr);
        }
    }
    pool.wait_drained();

    for (std::size_t s = 0; s < shard_count; ++s) {
        EXPECT_EQ(pool.shard_size(s), static_cast<std::size_t>(keys_per_shard));
        EXPECT_EQ(pool.shard_processed_count(s), static_cast<std::uint64_t>(keys_per_shard));
    }
}

TEST(ShardedTrackerPool, DestructingWithNoPendingWorkJoinsCleanly) {
    { ShardedTrackerPool pool(4); }  // destroyed immediately; workers must still join
    SUCCEED();
}

TEST(ShardedTrackerPool, PinWorkersToCoresDoesNotAffectCorrectness) {
    ShardedTrackerPool pool(4, /*pin_workers_to_cores=*/true);
    pool.submit(0, 1, nullptr);
    pool.submit(0, 1, nullptr);
    pool.wait_drained();

    EXPECT_EQ(pool.snapshot(1, 0).seen_count, 2U);
}

}  // namespace
}  // namespace ch08
