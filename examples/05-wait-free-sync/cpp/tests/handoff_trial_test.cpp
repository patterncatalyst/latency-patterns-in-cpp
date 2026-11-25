#include "ch05/handoff_trial.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace ch05 {
namespace {

// Small enough to run fast under every lane, including TSan; large enough
// that the two threads actually interleave rather than one finishing
// before the other starts (see ch04's false_sharing_test.cpp for the same
// sizing rationale).
constexpr std::uint64_t small_item_count = 5000;

TEST(HandoffTrial, MutexLaneDeliversEveryItemExactlyOnceInOrder) {
    const HandoffTrialResult result = run_mutex_handoff_trial(small_item_count);
    EXPECT_EQ(result.items_sent, small_item_count);
    EXPECT_EQ(result.items_received, small_item_count);
    EXPECT_EQ(result.dropped_count, 0U);
    EXPECT_EQ(result.out_of_order_count, 0U);
    EXPECT_EQ(result.duplicate_count, 0U);
    EXPECT_EQ(result.latencies_seconds.size(), small_item_count);
}

TEST(HandoffTrial, RingLaneDeliversEveryItemExactlyOnceInOrder) {
    const HandoffTrialResult result = run_ring_handoff_trial(small_item_count);
    EXPECT_EQ(result.items_sent, small_item_count);
    EXPECT_EQ(result.items_received, small_item_count);
    EXPECT_EQ(result.dropped_count, 0U);
    EXPECT_EQ(result.out_of_order_count, 0U);
    EXPECT_EQ(result.duplicate_count, 0U);
    EXPECT_EQ(result.latencies_seconds.size(), small_item_count);
}

// The chapter's central correctness claim: swapping the queue never drops,
// duplicates, or reorders a decision. Repeated across several trials since
// a race, if one existed, would not necessarily show up on the first run.
TEST(HandoffTrial, BothLanesAgreeOnDeliveryAcrossRepeatedTrials) {
    constexpr int trials = 3;
    for (int trial = 0; trial < trials; ++trial) {
        const HandoffTrialResult mutex_result = run_mutex_handoff_trial(small_item_count);
        const HandoffTrialResult ring_result = run_ring_handoff_trial(small_item_count);
        EXPECT_EQ(mutex_result.items_received, small_item_count);
        EXPECT_EQ(ring_result.items_received, small_item_count);
        EXPECT_EQ(mutex_result.out_of_order_count, 0U);
        EXPECT_EQ(ring_result.out_of_order_count, 0U);
    }
}

}  // namespace
}  // namespace ch05
