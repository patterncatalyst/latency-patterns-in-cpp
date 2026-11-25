#include "ch02/budget_model.hpp"

#include <cassert>
#include <numeric>
#include <stdexcept>

namespace ch02 {

Sample compute_mean(const std::vector<Sample>& samples) {
    if (samples.empty()) {
        throw std::logic_error("compute_mean: no samples");
    }
    const Sample total = std::accumulate(samples.begin(), samples.end(), Sample::zero());
    return total / static_cast<double>(samples.size());
}

double little_law_in_flight_estimate(double arrival_rate_per_s, double residence_time_s) {
    return arrival_rate_per_s * residence_time_s;
}

double amdahl_speedup(double parallel_fraction, double workers) {
    assert(parallel_fraction >= 0.0 && parallel_fraction <= 1.0);
    assert(workers >= 1.0);
    const double serial_fraction = 1.0 - parallel_fraction;
    return 1.0 / (serial_fraction + (parallel_fraction / workers));
}

}  // namespace ch02
