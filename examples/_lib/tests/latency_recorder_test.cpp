#include "measure/latency_recorder.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <numeric>

namespace measure {
namespace {

using std::chrono::duration;

LatencyRecorder recorder_with_seconds(std::initializer_list<double> seconds) {
    LatencyRecorder recorder;
    for (double s : seconds) {
        recorder.record(duration<double>(s));
    }
    return recorder;
}

TEST(LatencyRecorder, PercentileUsesNearestRankOnOneToOneHundred) {
    std::vector<double> values(100);
    // std::ranges::iota does not accept double: its value parameter must be
    // weakly_incrementable, which libstdc++ only wires up for integer-like
    // types (no incrementable_traits specialization for floating point).
    std::iota(values.begin(), values.end(), 1.0);  // NOLINT(modernize-use-ranges)

    LatencyRecorder recorder;
    for (double v : values) {
        recorder.record(duration<double>(v));
    }

    EXPECT_DOUBLE_EQ(recorder.percentile(50), 50.0);
    EXPECT_DOUBLE_EQ(recorder.percentile(99), 99.0);
    EXPECT_DOUBLE_EQ(recorder.percentile(99.9), 100.0);
    EXPECT_DOUBLE_EQ(recorder.percentile(100), 100.0);
}

TEST(LatencyRecorder, PercentileRejectsOutOfRangeRequests) {
    LatencyRecorder recorder = recorder_with_seconds({1.0});
    EXPECT_THROW((void)recorder.percentile(0.0), std::invalid_argument);
    EXPECT_THROW((void)recorder.percentile(100.1), std::invalid_argument);
}

TEST(LatencyRecorder, PercentileOnEmptyRecorderThrows) {
    LatencyRecorder recorder;
    EXPECT_THROW((void)recorder.percentile(50), std::logic_error);
}

TEST(LatencyRecorder, HistogramBucketsEveryRecordedSample) {
    LatencyRecorder recorder = recorder_with_seconds({0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0});
    const Histogram hist = recorder.histogram(5);

    EXPECT_DOUBLE_EQ(hist.min_value, 0.0);
    EXPECT_DOUBLE_EQ(hist.max_value, 9.0);
    EXPECT_DOUBLE_EQ(hist.bucket_width, 9.0 / 5.0);

    const std::uint64_t total =
        std::accumulate(hist.counts.begin(), hist.counts.end(), std::uint64_t{0});
    EXPECT_EQ(total, recorder.sample_count());
    EXPECT_EQ(hist.counts.size(), 5U);
    // 10 evenly spaced samples over 5 equal-width buckets: 2 per bucket.
    for (std::uint64_t count : hist.counts) {
        EXPECT_EQ(count, 2U);
    }
}

TEST(LatencyRecorder, HistogramOnConstantSamplesPutsEverythingInOneBucket) {
    LatencyRecorder recorder = recorder_with_seconds({5.0, 5.0, 5.0});
    const Histogram hist = recorder.histogram(4);

    EXPECT_EQ(hist.counts[0], 3U);
    for (std::size_t i = 1; i < hist.counts.size(); ++i) {
        EXPECT_EQ(hist.counts[i], 0U);
    }
}

TEST(LatencyRecorder, EcdfIsSortedAndReachesOne) {
    LatencyRecorder recorder = recorder_with_seconds({3.0, 1.0, 2.0});
    const auto points = recorder.ecdf();

    ASSERT_EQ(points.size(), 3U);
    EXPECT_DOUBLE_EQ(points[0].first, 1.0);
    EXPECT_DOUBLE_EQ(points[1].first, 2.0);
    EXPECT_DOUBLE_EQ(points[2].first, 3.0);
    EXPECT_DOUBLE_EQ(points.back().second, 1.0);
    for (std::size_t i = 1; i < points.size(); ++i) {
        EXPECT_GE(points[i].second, points[i - 1].second);
    }
}

TEST(LatencyRecorder, CoordinatedOmissionBackfillsMissedArrivals) {
    // expected interval 1.0s; one operation took 3.5s, so three arrivals
    // that should have landed at 1.0s, 2.0s, and 3.0s into that operation
    // were coordinated-omitted. record() backfills them.
    LatencyRecorder recorder{duration<double>(1.0)};
    recorder.record(duration<double>(3.5));

    EXPECT_EQ(recorder.sample_count(), 4U);
    EXPECT_DOUBLE_EQ(recorder.percentile(100), 3.5);
    // The backfilled samples are 2.5, 1.5, 0.5; sorted with the original 3.5
    // that is [0.5, 1.5, 2.5, 3.5], so the median (rank 2 of 4) is 1.5.
    EXPECT_DOUBLE_EQ(recorder.percentile(50), 1.5);
}

TEST(LatencyRecorder, NoExpectedIntervalMeansNoBackfill) {
    LatencyRecorder recorder;  // expected_interval defaults to zero: correction off
    recorder.record(duration<double>(50.0));
    EXPECT_EQ(recorder.sample_count(), 1U);
}

TEST(LatencyRecorder, SampleBelowExpectedIntervalIsNotBackfilled) {
    LatencyRecorder recorder{duration<double>(1.0)};
    recorder.record(duration<double>(0.3));
    EXPECT_EQ(recorder.sample_count(), 1U);
}

}  // namespace
}  // namespace measure
