#include "ch08/event_generator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace ch08 {
namespace {

TEST(GenerateUniformEvents, ProducesTheRequestedCountWithinKeySpace) {
    const auto events = generate_uniform_events(5000, 200, 1);
    EXPECT_EQ(events.size(), 5000U);
    for (const Barcode key : events) {
        EXPECT_GE(key, 1U);
        EXPECT_LE(key, 200U);
    }
}

TEST(GenerateUniformEvents, SameSeedProducesTheSameTrace) {
    const auto first = generate_uniform_events(500, 50, 42);
    const auto second = generate_uniform_events(500, 50, 42);
    EXPECT_EQ(first, second);
}

TEST(GenerateSkewedEvents, ProducesTheRequestedCount) {
    const std::vector<Barcode> hot_keys{3, 11};
    const auto events = generate_skewed_events(10'000, hot_keys, 0.6, 1000, 7);
    EXPECT_EQ(events.size(), 10'000U);
}

TEST(GenerateSkewedEvents, ColdEventsAreNeverEqualToAHotKey) {
    const std::vector<Barcode> hot_keys{3, 11};
    const std::unordered_set<Barcode> hot_set(hot_keys.begin(), hot_keys.end());
    const auto events = generate_skewed_events(10'000, hot_keys, 0.6, 1000, 7);

    for (const Barcode key : events) {
        if (!hot_set.contains(key)) {
            // Every "cold" event must come from the cold key space, never
            // happen to collide with a hot key's own value.
            EXPECT_TRUE(key != hot_keys[0] && key != hot_keys[1]);
        }
    }
}

TEST(GenerateSkewedEvents, HotFractionLandsCloseToTheRequestedShare) {
    const std::vector<Barcode> hot_keys{3, 11};
    constexpr std::uint64_t total = 100'000;
    const auto events = generate_skewed_events(total, hot_keys, 0.6, 5000, 99);

    const auto hot_count = static_cast<double>(
        std::count_if(events.begin(), events.end(), [&](Barcode k) { return k == 3 || k == 11; }));
    const double hot_share = hot_count / static_cast<double>(total);

    // A random draw at a 60% target over 100,000 samples should land within
    // a few percentage points; this is a sanity bound, not a statistical
    // claim this chapter's prose reports.
    EXPECT_NEAR(hot_share, 0.6, 0.02);
}

}  // namespace
}  // namespace ch08
