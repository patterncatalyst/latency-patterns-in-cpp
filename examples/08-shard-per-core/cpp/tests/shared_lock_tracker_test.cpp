#include "ch08/shared_lock_tracker.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace ch08 {
namespace {

TEST(SharedLockTracker, SnapshotOfAnUnseenKeyIsDefault) {
    const SharedLockTracker tracker;
    const TrackedState state = tracker.snapshot(999);
    EXPECT_EQ(state.seen_count, 0U);
    EXPECT_EQ(state.last_seen_us, 0.0);
}

TEST(SharedLockTracker, ObserveIncrementsCountAndUpdatesLastSeen) {
    SharedLockTracker tracker;

    tracker.observe(7, 10.0);
    tracker.observe(7, 20.0);
    tracker.observe(7, 30.0);

    const TrackedState state = tracker.snapshot(7);
    EXPECT_EQ(state.seen_count, 3U);
    EXPECT_EQ(state.last_seen_us, 30.0);
}

TEST(SharedLockTracker, EntryCountReflectsDistinctKeysOnly) {
    SharedLockTracker tracker;
    tracker.observe(1, 1.0);
    tracker.observe(1, 2.0);
    tracker.observe(2, 3.0);

    EXPECT_EQ(tracker.entry_count(), 2U);
}

// Many threads hammering the same handful of keys through the one shared
// lock: every observation must still land, none lost to a missed update.
TEST(SharedLockTracker, ConcurrentObserveLosesNoUpdates) {
    constexpr int thread_count = 8;
    constexpr int observations_per_thread = 2000;
    SharedLockTracker tracker;

    std::vector<std::jthread> workers;
    workers.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        workers.emplace_back([&tracker] {
            for (int i = 0; i < observations_per_thread; ++i) {
                tracker.observe(1, static_cast<double>(i));
            }
        });
    }
    workers.clear();  // join every worker before reading the result

    EXPECT_EQ(tracker.snapshot(1).seen_count, static_cast<std::uint64_t>(thread_count * observations_per_thread));
}

}  // namespace
}  // namespace ch08
