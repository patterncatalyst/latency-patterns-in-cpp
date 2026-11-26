#include "ch07/zipf_trace.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace ch07 {

ZipfTrace::ZipfTrace(int key_space, double skew, std::uint64_t seed)
    : key_space_(key_space), seed_(seed) {
    std::vector<double> weights(static_cast<std::size_t>(key_space));
    for (int rank = 0; rank < key_space; ++rank) {
        weights[static_cast<std::size_t>(rank)] = 1.0 / std::pow(static_cast<double>(rank + 1), skew);
    }
    const double total = std::accumulate(weights.begin(), weights.end(), 0.0);

    cumulative_.resize(weights.size());
    double running = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        running += weights[i] / total;
        cumulative_[i] = running;
    }
    cumulative_.back() = 1.0;  // guard against floating-point drift

    rank_to_key_.resize(static_cast<std::size_t>(key_space));
    std::ranges::iota(rank_to_key_, 0);
    std::mt19937_64 shuffle_rng(seed_ ^ 0x9E3779B97F4A7C15ULL);
    std::ranges::shuffle(rank_to_key_, shuffle_rng);
}

std::vector<int> ZipfTrace::generate(std::size_t length) const {
    std::vector<int> trace;
    trace.reserve(length);
    std::mt19937_64 draw_rng(seed_);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    for (std::size_t i = 0; i < length; ++i) {
        const double u = uniform(draw_rng);
        const auto it = std::ranges::lower_bound(cumulative_, u);
        const auto rank = static_cast<std::size_t>(std::distance(cumulative_.begin(), it));
        trace.push_back(rank_to_key_[std::min(rank, rank_to_key_.size() - 1)]);
    }
    return trace;
}

std::vector<int> ZipfTrace::hottest_keys(int count) const {
    std::vector<int> keys;
    const int limit = std::min(count, key_space_);
    keys.reserve(static_cast<std::size_t>(limit));
    for (int rank = 0; rank < limit; ++rank) {
        keys.push_back(rank_to_key_[static_cast<std::size_t>(rank)]);
    }
    return keys;
}

}  // namespace ch07
