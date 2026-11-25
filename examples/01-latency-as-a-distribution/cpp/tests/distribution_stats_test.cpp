#include "ch01/distribution_stats.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <vector>

namespace ch01 {
namespace {

using namespace std::chrono_literals;

TEST(ComputeMean, AveragesASmallKnownSet) {
    const std::vector<Sample> samples{Sample(1ms), Sample(2ms), Sample(3ms)};
    EXPECT_NEAR(compute_mean(samples).count(), std::chrono::duration<double>(2ms).count(), 1e-9);
}

TEST(ComputeMean, OneRareLargeSampleDominatesTheMean) {
    // 99 samples at 1us, one at 10ms: the mean sits far from what 99% of the
    // samples actually experienced, which is the gap this chapter's example
    // measures for real (see ch01::build_naive_and_corrected and the
    // chapter's results file).
    std::vector<Sample> samples(99, Sample(std::chrono::microseconds(1)));
    samples.emplace_back(10ms);

    const double mean_us = compute_mean(samples).count() * 1e6;
    EXPECT_GT(mean_us, 100.0);   // pulled far above the 1us typical sample
    EXPECT_LT(mean_us, 10000.0);  // but nowhere near the 10ms outlier either
}

TEST(ComputeMean, ThrowsOnEmptyInput) {
    const std::vector<Sample> empty;
    EXPECT_THROW((void)compute_mean(empty), std::logic_error);
}

TEST(BuildNaiveAndCorrected, ZeroExpectedIntervalLeavesBothUncorrected) {
    const std::vector<Sample> samples{Sample(1ms), Sample(2ms), Sample(50ms)};
    const NaiveAndCorrected dist = build_naive_and_corrected(samples, Sample::zero());

    EXPECT_EQ(dist.naive.sample_count(), samples.size());
    EXPECT_EQ(dist.corrected.sample_count(), samples.size());
}

TEST(BuildNaiveAndCorrected, ABackToBackStallOnlyMovesTheCorrectedCount) {
    // One parcel that stalls for 5ms against a 1ms expected interval implies
    // 4 more arrivals piled up behind it (5ms - 1ms = 4ms, backfilled every
    // 1ms): see LatencyRecorder::record()'s loop. The naive recorder never
    // sees them; the corrected one does.
    const std::vector<Sample> samples{Sample{5ms}};
    const Sample expected_interval{1ms};

    const NaiveAndCorrected dist = build_naive_and_corrected(samples, expected_interval);

    EXPECT_EQ(dist.naive.sample_count(), 1U);
    EXPECT_EQ(dist.corrected.sample_count(), 5U);  // the 1 raw sample + 4 backfilled
}

TEST(BuildNaiveAndCorrected, CorrectedTailMovesMaterially) {
    // The scenario this chapter's example runs, shrunk down: a population
    // that is overwhelmingly fast with a rare, much larger stall. Naive p99
    // can miss the stall outright when the stall rate sits below the top-1%
    // cutoff; the corrected recorder, backfilling the arrivals the stall
    // implies, should not.
    std::vector<Sample> samples(995, Sample(std::chrono::microseconds(5)));
    for (int i = 0; i < 5; ++i) {
        samples.emplace_back(8ms);
    }
    const Sample expected_interval{std::chrono::microseconds(500)};

    const NaiveAndCorrected dist = build_naive_and_corrected(samples, expected_interval);
    const double naive_p99_us = dist.naive.percentile(99) * 1e6;
    const double corrected_p99_us = dist.corrected.percentile(99) * 1e6;

    EXPECT_LT(naive_p99_us, 100.0);          // naive p99 still sits on the fast path
    EXPECT_GT(corrected_p99_us, 1000.0);     // corrected p99 lands inside the stall tail
    EXPECT_GT(dist.corrected.sample_count(), dist.naive.sample_count());
}

}  // namespace
}  // namespace ch01
