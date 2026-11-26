#include "ch08/event_generator.hpp"

#include <random>

namespace ch08 {

namespace {

// Cold keys in the skewed trace start well above the range
// find_colliding_hot_keys scans (see routing.hpp's default scan_limit), so a
// cold key can never accidentally equal a hot one.
constexpr std::uint64_t cold_key_offset = 10'000'000ULL;

}  // namespace

std::vector<Barcode> generate_uniform_events(std::uint64_t count, std::uint64_t key_space, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::uint64_t> key_dist(1, key_space);
    std::vector<Barcode> events;
    events.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        events.push_back(key_dist(rng));
    }
    return events;
}

std::vector<Barcode> generate_skewed_events(std::uint64_t count, const std::vector<Barcode>& hot_keys,
                                             double hot_fraction, std::uint64_t cold_key_space,
                                             std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::bernoulli_distribution is_hot(hot_fraction);
    std::uniform_int_distribution<std::size_t> hot_pick(0, hot_keys.size() - 1);
    std::uniform_int_distribution<std::uint64_t> cold_pick(cold_key_offset + 1, cold_key_offset + cold_key_space);

    std::vector<Barcode> events;
    events.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        events.push_back(is_hot(rng) ? hot_keys[hot_pick(rng)] : cold_pick(rng));
    }
    return events;
}

}  // namespace ch08
