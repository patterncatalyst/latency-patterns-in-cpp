#include "ch01/distribution_stats.hpp"

#include <numeric>
#include <stdexcept>

namespace ch01 {

Sample compute_mean(const std::vector<Sample>& samples) {
    if (samples.empty()) {
        throw std::logic_error("compute_mean: no samples");
    }
    const Sample total = std::accumulate(samples.begin(), samples.end(), Sample::zero());
    return total / static_cast<double>(samples.size());
}

NaiveAndCorrected build_naive_and_corrected(const std::vector<Sample>& samples,
                                             Sample expected_interval) {
    NaiveAndCorrected result{
        .naive = measure::LatencyRecorder{},
        .corrected = measure::LatencyRecorder{expected_interval},
    };
    for (const Sample& sample : samples) {
        result.naive.record(sample);
        result.corrected.record(sample);
    }
    return result;
}

}  // namespace ch01
