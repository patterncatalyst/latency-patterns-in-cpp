#include "measure/latency_recorder.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace measure {

LatencyRecorder::LatencyRecorder(std::chrono::duration<double> expected_interval)
    : expected_interval_(expected_interval) {}

void LatencyRecorder::record(std::chrono::duration<double> latency) {
    samples_.push_back(latency.count());

    const double expected = expected_interval_.count();
    if (expected <= 0.0) {
        return;  // no expected-interval configured; nothing to correct
    }

    // Coordinated-omission backfill: this operation ran `latency` seconds
    // while new arrivals were scheduled every `expected` seconds, so every
    // arrival piled up behind it experienced a proportionally larger wait.
    // Synthesize those missing samples instead of only recording the one
    // the generator happened to observe.
    double missed = latency.count() - expected;
    while (missed > 0.0) {
        samples_.push_back(missed);
        missed -= expected;
    }
}

double LatencyRecorder::percentile(double p) const {
    if (samples_.empty()) {
        throw std::logic_error("LatencyRecorder::percentile: no samples recorded");
    }
    if (p <= 0.0 || p > 100.0) {
        throw std::invalid_argument("LatencyRecorder::percentile: p must be in (0, 100]");
    }

    std::vector<double> sorted = samples_;
    std::ranges::sort(sorted);

    const auto rank =
        static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(sorted.size())));
    const std::size_t index = std::min(rank, sorted.size()) - 1;
    return sorted[index];
}

Histogram LatencyRecorder::histogram(std::size_t bucket_count) const {
    if (samples_.empty()) {
        throw std::logic_error("LatencyRecorder::histogram: no samples recorded");
    }
    if (bucket_count == 0) {
        throw std::invalid_argument("LatencyRecorder::histogram: bucket_count must be positive");
    }

    const auto [min_it, max_it] = std::ranges::minmax_element(samples_);
    const double min_value = *min_it;
    const double max_value = *max_it;
    const double span = max_value - min_value;
    const double bucket_width = span > 0.0 ? span / static_cast<double>(bucket_count) : 1.0;

    Histogram hist{
        .min_value = min_value,
        .max_value = max_value,
        .bucket_width = bucket_width,
        .counts = std::vector<std::uint64_t>(bucket_count, 0),
    };

    for (double sample : samples_) {
        std::size_t bucket = span > 0.0
                                  ? static_cast<std::size_t>((sample - min_value) / bucket_width)
                                  : 0;
        bucket = std::min(bucket, bucket_count - 1);
        ++hist.counts[bucket];
    }
    return hist;
}

std::vector<std::pair<double, double>> LatencyRecorder::ecdf() const {
    if (samples_.empty()) {
        return {};
    }

    std::vector<double> sorted = samples_;
    std::ranges::sort(sorted);

    std::vector<std::pair<double, double>> points;
    points.reserve(sorted.size());
    const auto n = static_cast<double>(sorted.size());
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        points.emplace_back(sorted[i], static_cast<double>(i + 1) / n);
    }
    return points;
}

}  // namespace measure
