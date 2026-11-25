#include "ch05/mutex_handoff_queue.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <thread>
#include <vector>

namespace ch05 {
namespace {

TEST(MutexHandoffQueue, PopReturnsItemsInFifoOrder) {
    MutexHandoffQueue queue;
    Decision first;
    first.parcel_id = 1;
    Decision second;
    second.parcel_id = 2;
    queue.push(first);
    queue.push(second);

    Decision out;
    ASSERT_TRUE(queue.pop(out));
    EXPECT_EQ(out.parcel_id, 1U);
    ASSERT_TRUE(queue.pop(out));
    EXPECT_EQ(out.parcel_id, 2U);
}

TEST(MutexHandoffQueue, CloseWithEmptyQueueMakesPopReturnFalse) {
    MutexHandoffQueue queue;
    queue.close();
    Decision out;
    EXPECT_FALSE(queue.pop(out));
}

TEST(MutexHandoffQueue, CloseAfterPushStillDeliversWhatWasQueued) {
    MutexHandoffQueue queue;
    Decision decision;
    decision.parcel_id = 7;
    queue.push(decision);
    queue.close();

    Decision out;
    ASSERT_TRUE(queue.pop(out));
    EXPECT_EQ(out.parcel_id, 7U);
    EXPECT_FALSE(queue.pop(out));
}

// Correctness under real concurrency: one producer thread, one consumer
// thread, no lost or duplicated items, FIFO order preserved. Small enough
// to run fast under TSan while still forcing real interleaving.
TEST(MutexHandoffQueue, ConcurrentProducerConsumerLosesOrDuplicatesNothing) {
    constexpr std::uint64_t item_count = 5000;
    MutexHandoffQueue queue;
    std::vector<std::uint64_t> received;
    received.reserve(item_count);

    std::thread consumer([&] {
        Decision decision;
        while (queue.pop(decision)) {
            received.push_back(decision.parcel_id);
        }
    });

    for (std::uint64_t parcel_id = 0; parcel_id < item_count; ++parcel_id) {
        Decision decision;
        decision.parcel_id = parcel_id;
        queue.push(decision);
    }
    queue.close();
    consumer.join();

    ASSERT_EQ(received.size(), item_count);
    for (std::uint64_t i = 0; i < item_count; ++i) {
        EXPECT_EQ(received[i], i) << "FIFO order broken at position " << i;
    }
}

}  // namespace
}  // namespace ch05
