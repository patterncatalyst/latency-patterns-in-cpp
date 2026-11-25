#include "ch05/spsc_ring_queue.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <thread>
#include <vector>

#include "ch05/decision.hpp"

namespace ch05 {
namespace {

TEST(SpscRingQueue, TryPopOnEmptyRingReturnsFalse) {
    SpscRingQueue<int, 4> ring;
    int out = 0;
    EXPECT_FALSE(ring.try_pop(out));
}

TEST(SpscRingQueue, PushThenPopReturnsTheSameValueInFifoOrder) {
    SpscRingQueue<int, 4> ring;
    ASSERT_TRUE(ring.try_push(10));
    ASSERT_TRUE(ring.try_push(20));

    int out = 0;
    ASSERT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out, 10);
    ASSERT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out, 20);
    EXPECT_FALSE(ring.try_pop(out));
}

// A ring of Capacity N holds at most N-1 live items: one slot stays empty
// so the head==tail test can tell "empty" from "full" without a separate
// counter (see spsc_ring_queue.hpp). This checks that boundary exactly.
TEST(SpscRingQueue, FullRingRejectsOneMoreThanItsUsableCapacity) {
    SpscRingQueue<int, 4> ring;
    EXPECT_EQ(ring.capacity(), 3U);
    ASSERT_TRUE(ring.try_push(1));
    ASSERT_TRUE(ring.try_push(2));
    ASSERT_TRUE(ring.try_push(3));
    EXPECT_FALSE(ring.try_push(4));  // full

    int out = 0;
    ASSERT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out, 1);
    EXPECT_TRUE(ring.try_push(4));  // room again after one pop
}

TEST(SpscRingQueue, WrapsAroundThePhysicalArrayWithoutLosingOrder) {
    SpscRingQueue<int, 4> ring;
    for (int round = 0; round < 10; ++round) {
        ASSERT_TRUE(ring.try_push(round));
        int out = -1;
        ASSERT_TRUE(ring.try_pop(out));
        EXPECT_EQ(out, round);
    }
}

// Correctness under real concurrency: one producer thread, one consumer
// thread, no lost or duplicated items, FIFO order preserved. This is the
// property TSan's lane exercises directly against the acquire/release pair
// in try_push/try_pop.
TEST(SpscRingQueue, ConcurrentProducerConsumerLosesOrDuplicatesNothing) {
    constexpr std::uint64_t item_count = 5000;
    constexpr std::size_t test_ring_capacity = 256;
    SpscRingQueue<Decision, test_ring_capacity> ring;
    std::vector<std::uint64_t> received;
    received.reserve(item_count);

    std::thread consumer([&] {
        Decision decision;
        while (received.size() < item_count) {
            if (ring.try_pop(decision)) {
                received.push_back(decision.parcel_id);
            }
        }
    });

    for (std::uint64_t parcel_id = 0; parcel_id < item_count; ++parcel_id) {
        Decision decision;
        decision.parcel_id = parcel_id;
        while (!ring.try_push(decision)) {
            // Full; the consumer thread will make room shortly.
        }
    }
    consumer.join();

    ASSERT_EQ(received.size(), item_count);
    for (std::uint64_t i = 0; i < item_count; ++i) {
        EXPECT_EQ(received[i], i) << "FIFO order broken at position " << i;
    }
}

}  // namespace
}  // namespace ch05
