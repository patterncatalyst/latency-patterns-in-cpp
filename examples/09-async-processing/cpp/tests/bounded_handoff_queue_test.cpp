#include "ch09/bounded_handoff_queue.hpp"

#include <unistd.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace ch09 {
namespace {

using Queue = BoundedHandoffQueue<int, 8>;

TEST(BoundedHandoffQueueTest, PushThenDrainPreservesOrder) {
    Queue queue;
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(queue.push_with_backpressure(i, std::chrono::microseconds(1000)));
    }
    std::vector<int> out;
    const std::size_t popped = queue.drain(out, 10);
    EXPECT_EQ(popped, 5U);
    EXPECT_EQ(out, (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(BoundedHandoffQueueTest, DrainNeverExceedsMaxItems) {
    Queue queue;
    for (int i = 0; i < 7; ++i) {
        ASSERT_TRUE(queue.push_with_backpressure(i, std::chrono::microseconds(1000)));
    }
    std::vector<int> out;
    const std::size_t popped = queue.drain(out, 3);
    EXPECT_EQ(popped, 3U);
    EXPECT_EQ(out, (std::vector<int>{0, 1, 2}));
}

TEST(BoundedHandoffQueueTest, ShedsWithAccountingWhenFullPastMaxWait) {
    Queue queue;  // capacity() == 7
    for (std::size_t i = 0; i < Queue::capacity(); ++i) {
        ASSERT_TRUE(queue.push_with_backpressure(static_cast<int>(i), std::chrono::microseconds(1000)));
    }
    // The ring is now full; nobody ever drains it in this test, so the next
    // push must wait out max_wait and then shed, not block forever.
    const auto start = std::chrono::steady_clock::now();
    const bool pushed = queue.push_with_backpressure(999, std::chrono::microseconds(5000));
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(pushed);
    EXPECT_EQ(queue.shed_count(), 1U);
    EXPECT_GE(elapsed, std::chrono::microseconds(4000));  // actually waited out the bound, did not shed instantly
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));   // and did not block far past it either
}

TEST(BoundedHandoffQueueTest, MakingRoomBeforeMaxWaitLetsThePushSucceed) {
    Queue queue;
    for (std::size_t i = 0; i < Queue::capacity(); ++i) {
        ASSERT_TRUE(queue.push_with_backpressure(static_cast<int>(i), std::chrono::microseconds(1000)));
    }
    std::thread drainer([&queue] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::vector<int> out;
        queue.drain(out, 1);
    });
    const bool pushed = queue.push_with_backpressure(999, std::chrono::milliseconds(200));
    drainer.join();
    EXPECT_TRUE(pushed);
    EXPECT_EQ(queue.shed_count(), 0U);
}

TEST(BoundedHandoffQueueTest, HighWaterMarkTracksTheDeepestObservedDepth) {
    Queue queue;
    for (std::size_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(queue.push_with_backpressure(static_cast<int>(i), std::chrono::microseconds(1000)));
    }
    EXPECT_EQ(queue.high_water_mark(), 4U);
    std::vector<int> out;
    queue.drain(out, 2);
    for (std::size_t i = 0; i < 2; ++i) {
        ASSERT_TRUE(queue.push_with_backpressure(static_cast<int>(i), std::chrono::microseconds(1000)));
    }
    EXPECT_EQ(queue.high_water_mark(), 4U);  // never dropped back down after a lower-depth push
}

TEST(BoundedHandoffQueueTest, EventFdSignalsOnPush) {
    Queue queue;
    ASSERT_TRUE(queue.push_with_backpressure(1, std::chrono::microseconds(1000)));
    std::uint64_t value = 0;
    const ssize_t result = ::read(queue.event_fd(), &value, sizeof(value));
    EXPECT_EQ(result, static_cast<ssize_t>(sizeof(value)));
    EXPECT_GE(value, 1U);
}

}  // namespace
}  // namespace ch09
