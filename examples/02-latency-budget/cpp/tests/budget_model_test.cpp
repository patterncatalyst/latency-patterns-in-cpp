#include "ch02/budget_model.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <vector>

namespace ch02 {
namespace {

using namespace std::chrono_literals;

TEST(ComputeMean, AveragesASmallKnownSet) {
    const std::vector<Sample> samples{Sample(1ms), Sample(2ms), Sample(3ms)};
    EXPECT_NEAR(compute_mean(samples).count(), std::chrono::duration<double>(2ms).count(), 1e-9);
}

TEST(ComputeMean, ThrowsOnEmptyInput) {
    const std::vector<Sample> empty;
    EXPECT_THROW((void)compute_mean(empty), std::logic_error);
}

TEST(LittleLaw, MatchesTheHouseDefaultBeltGeometryByHand) {
    // House default belt: 2.0 m/s, 1.5 m scan-to-diverter distance, so the
    // deadline is 0.75s (see CLAUDE.md, "The spine"); at the house default
    // arrival rate of 10 parcels/s, Little's Law says 7.5 parcels occupy
    // that segment at any instant, the same hand check the running example
    // and harness chapter's deadline cross-check works the same way.
    EXPECT_DOUBLE_EQ(little_law_in_flight_estimate(10.0, 0.75), 7.5);
}

TEST(LittleLaw, ScalesLinearlyWithArrivalRate) {
    const double base = little_law_in_flight_estimate(10.0, 0.75);
    const double doubled = little_law_in_flight_estimate(20.0, 0.75);
    EXPECT_DOUBLE_EQ(doubled, base * 2.0);
}

TEST(AmdahlSpeedup, ZeroParallelFractionNeverSpeedsUpRegardlessOfWorkers) {
    EXPECT_DOUBLE_EQ(amdahl_speedup(0.0, 2.0), 1.0);
    EXPECT_DOUBLE_EQ(amdahl_speedup(0.0, 64.0), 1.0);
}

TEST(AmdahlSpeedup, FullyParallelMatchesWorkerCountExactly) {
    EXPECT_DOUBLE_EQ(amdahl_speedup(1.0, 4.0), 4.0);
}

TEST(AmdahlSpeedup, MoreWorkersNeverHelpsPastTheSerialCeiling) {
    // A stage that is 80% parallelizable can never speed up more than 5x
    // (1 / (1 - 0.8)), no matter how many workers attack the parallel part;
    // the speedup at 1000 workers should sit close to, but never past, that
    // ceiling.
    const double near_ceiling = amdahl_speedup(0.8, 1000.0);
    EXPECT_LE(near_ceiling, 5.0);
    EXPECT_GT(near_ceiling, 4.9);
}

}  // namespace
}  // namespace ch02
