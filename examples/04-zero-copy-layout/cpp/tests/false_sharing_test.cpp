#include "ch04/false_sharing.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace ch04 {
namespace {

// Small enough to run fast under every lane, including TSan; large enough
// that two threads actually interleave rather than one finishing before the
// other starts.
constexpr std::uint64_t small_iterations = 20'000;

TEST(FalseSharingTrial, UnpaddedCountersStillReachTheExactIterationCountOnBothThreads) {
    const FalseSharingTrialResult result = run_unpadded_trial(small_iterations);
    EXPECT_EQ(result.scan_total, small_iterations);
    EXPECT_EQ(result.divert_total, small_iterations);
}

TEST(FalseSharingTrial, PaddedCountersReachTheExactIterationCountOnBothThreads) {
    const FalseSharingTrialResult result = run_padded_trial(small_iterations);
    EXPECT_EQ(result.scan_total, small_iterations);
    EXPECT_EQ(result.divert_total, small_iterations);
}

// The chapter's central correctness claim for this lane: padding fixes a
// performance problem, not a correctness one. Both layouts must land on
// identical final counts every time, across repeated trials.
TEST(FalseSharingTrial, PaddingNeverChangesTheFinalCount) {
    constexpr int trials = 5;
    for (int trial = 0; trial < trials; ++trial) {
        const FalseSharingTrialResult unpadded = run_unpadded_trial(small_iterations);
        const FalseSharingTrialResult padded = run_padded_trial(small_iterations);
        EXPECT_EQ(unpadded.scan_total, padded.scan_total);
        EXPECT_EQ(unpadded.divert_total, padded.divert_total);
        EXPECT_EQ(unpadded.scan_total, small_iterations);
    }
}

}  // namespace
}  // namespace ch04
