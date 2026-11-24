#pragma once

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace measure {

// A fixed-width histogram over the recorded samples (seconds).
struct Histogram {
    double min_value = 0.0;
    double max_value = 0.0;
    double bucket_width = 0.0;
    std::vector<std::uint64_t> counts;
};

// Records a latency distribution and reports it the way CLAUDE.md requires:
// p50/p99/p99.9 plus a histogram or eCDF, never a mean (see "Measurement
// discipline"). A mean never appears on this type's public surface.
//
// Give it the schedule's expected inter-arrival interval and record() also
// performs Gil Tene's coordinated-omission correction: the belt never waits
// (see CLAUDE.md, "Domain"), so a sample that ran longer than one scheduled
// interval implies every arrival queued behind it waited a proportionally
// longer time too, and those missing samples are backfilled rather than
// silently dropped.
class LatencyRecorder {
public:
    explicit LatencyRecorder(
        std::chrono::duration<double> expected_interval = std::chrono::duration<double>::zero());

    void record(std::chrono::duration<double> latency);

    [[nodiscard]] std::size_t sample_count() const noexcept { return samples_.size(); }

    // p in (0, 100]; e.g. percentile(50), percentile(99), percentile(99.9).
    [[nodiscard]] double percentile(double p) const;

    [[nodiscard]] Histogram histogram(std::size_t bucket_count = 20) const;

    // (value, cumulative fraction) pairs, sorted by value.
    [[nodiscard]] std::vector<std::pair<double, double>> ecdf() const;

private:
    std::chrono::duration<double> expected_interval_;
    std::vector<double> samples_;  // seconds
};

}  // namespace measure
