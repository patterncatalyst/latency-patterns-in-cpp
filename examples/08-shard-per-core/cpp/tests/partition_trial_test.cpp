#include "ch08/partition_trial.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ch08/event_generator.hpp"
#include "ch08/routing.hpp"

namespace ch08 {
namespace {

constexpr std::uint64_t test_item_count = 20'000;
constexpr std::uint64_t test_key_space = 500;
constexpr std::size_t test_worker_count = 6;

TEST(RunSharedLockTrial, RecordsOneCompletionTimestampPerEvent) {
    const std::vector<Barcode> events = generate_uniform_events(test_item_count, test_key_space, 1);
    SharedLockTracker tracker;

    const PartitionTrialResult result = run_shared_lock_trial(events, tracker, test_worker_count);

    EXPECT_EQ(result.completion_us.size(), test_item_count);
    EXPECT_GT(result.total_wall_time_us, 0.0);
    for (const double us : result.completion_us) {
        EXPECT_GE(us, 0.0);
    }
}

TEST(RunSharedLockTrial, TallyMatchesAManualCountOfEachKey) {
    const std::vector<Barcode> events = generate_uniform_events(test_item_count, test_key_space, 2);
    SharedLockTracker tracker;
    run_shared_lock_trial(events, tracker, test_worker_count);

    std::unordered_map<Barcode, std::uint64_t> expected;
    for (const Barcode key : events) {
        ++expected[key];
    }
    for (const auto& [key, count] : expected) {
        EXPECT_EQ(tracker.snapshot(key).seen_count, count);
    }
}

TEST(RunShardedTrial, RecordsOneCompletionTimestampPerEvent) {
    const std::vector<Barcode> events = generate_uniform_events(test_item_count, test_key_space, 3);
    ShardedTrackerPool pool(test_worker_count);
    const auto router = [&](Barcode key) { return hash_route(key, pool.shard_count()); };

    const PartitionTrialResult result = run_sharded_trial(events, pool, router);

    EXPECT_EQ(result.completion_us.size(), test_item_count);
    EXPECT_GT(result.total_wall_time_us, 0.0);
    for (const double us : result.completion_us) {
        EXPECT_GE(us, 0.0);
    }
}

// The correctness check this chapter's measurement depends on: shard-per-
// core and the shared-lock baseline must agree on every key's seen_count
// after running over the identical event sequence.
TEST(RunShardedTrial, AgreesWithSharedLockTrackerOnEveryKeysSeenCount) {
    const std::vector<Barcode> events = generate_uniform_events(test_item_count, test_key_space, 4);

    SharedLockTracker shared_tracker;
    run_shared_lock_trial(events, shared_tracker, test_worker_count);

    ShardedTrackerPool pool(test_worker_count);
    const auto router = [&](Barcode key) { return hash_route(key, pool.shard_count()); };
    run_sharded_trial(events, pool, router);

    const std::unordered_set<Barcode> distinct_keys(events.begin(), events.end());
    for (const Barcode key : distinct_keys) {
        const std::size_t shard = hash_route(key, pool.shard_count());
        EXPECT_EQ(shared_tracker.snapshot(key).seen_count, pool.snapshot(key, shard).seen_count)
            << "key=" << key;
    }
}

// Skew must be detectable in the per-shard counts: a shard holding a hot
// key processes far more events than one that does not.
TEST(RunShardedTrial, SkewedTraceOverloadsTheHotKeysShard) {
    const std::vector<Barcode> hot_keys = find_colliding_hot_keys(test_worker_count, 2);
    ASSERT_EQ(hot_keys.size(), 2U);
    const std::size_t hot_shard = hash_route(hot_keys[0], test_worker_count);

    const std::vector<Barcode> events = generate_skewed_events(test_item_count, hot_keys, 0.6, 2000, 5);
    ShardedTrackerPool pool(test_worker_count);
    const auto router = [&](Barcode key) { return hash_route(key, pool.shard_count()); };
    run_sharded_trial(events, pool, router);

    std::uint64_t max_other_load = 0;
    for (std::size_t s = 0; s < pool.shard_count(); ++s) {
        if (s != hot_shard) {
            max_other_load = std::max(max_other_load, pool.shard_processed_count(s));
        }
    }

    EXPECT_GT(pool.shard_processed_count(hot_shard), max_other_load * 2);
}

}  // namespace
}  // namespace ch08
