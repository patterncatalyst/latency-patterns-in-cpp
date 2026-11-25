#include "ch07/zipf_trace.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <vector>

namespace ch07 {
namespace {

TEST(ZipfTraceTest, EveryGeneratedKeyIsInRange) {
    const ZipfTrace trace(100, 1.1, 1);
    for (const int key : trace.generate(5000)) {
        EXPECT_GE(key, 0);
        EXPECT_LT(key, 100);
    }
}

TEST(ZipfTraceTest, GenerateIsDeterministicForTheSameSeed) {
    const ZipfTrace first(100, 1.1, 42);
    const ZipfTrace second(100, 1.1, 42);
    EXPECT_EQ(first.generate(2000), second.generate(2000));
}

TEST(ZipfTraceTest, HottestKeysAccountForMostOfTheTraffic) {
    const ZipfTrace trace_gen(200, 1.2, 1);
    const std::vector<int> trace = trace_gen.generate(20000);
    const std::vector<int> hot = trace_gen.hottest_keys(10);

    std::size_t hot_hits = 0;
    for (const int key : trace) {
        if (std::ranges::find(hot, key) != hot.end()) {
            ++hot_hits;
        }
    }
    // 10 of 200 keys (5% of the key space) should carry well over 5% of the
    // traffic under a skewed distribution; a uniform trace would land near
    // 5%, which is exactly the "this would not differ" case the chapter
    // names as the reason it does not test the policies on one.
    const double hot_fraction = static_cast<double>(hot_hits) / static_cast<double>(trace.size());
    EXPECT_GT(hot_fraction, 0.30);
}

TEST(ZipfTraceTest, HottestKeysReturnsTheRequestedCountAndNoDuplicates) {
    const ZipfTrace trace_gen(50, 1.0, 3);
    const std::vector<int> hot = trace_gen.hottest_keys(8);
    EXPECT_EQ(hot.size(), 8U);
    std::vector<int> sorted_hot = hot;
    std::ranges::sort(sorted_hot);
    EXPECT_EQ(std::ranges::adjacent_find(sorted_hot), sorted_hot.end());
}

}  // namespace
}  // namespace ch07
