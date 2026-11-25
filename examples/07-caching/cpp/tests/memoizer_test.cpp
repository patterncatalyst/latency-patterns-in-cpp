#include "ch07/memoizer.hpp"

#include <gtest/gtest.h>

namespace ch07 {
namespace {

TEST(MemoizerTest, SecondCallWithTheSameKeyIsAHitAndDoesNotInvokeTheFunction) {
    int call_count = 0;
    auto square = [&call_count](int key) {
        ++call_count;
        return key * key;
    };
    Memoizer<int, int, decltype(square)> memoized(square);

    EXPECT_EQ(memoized(5), 25);
    EXPECT_EQ(call_count, 1);
    EXPECT_EQ(memoized(5), 25);
    EXPECT_EQ(call_count, 1);  // the underlying function was not called a second time

    EXPECT_EQ(memoized.hit_count(), 1U);
    EXPECT_EQ(memoized.miss_count(), 1U);
}

TEST(MemoizerTest, ReturnsIdenticalValuesToCallingTheFunctionDirectly) {
    auto cube = [](int key) { return key * key * key; };
    Memoizer<int, int, decltype(cube)> memoized(cube);

    for (int key = 0; key < 20; ++key) {
        EXPECT_EQ(memoized(key), cube(key));
    }
    // Asking a second time for every key already seen must still agree with
    // the direct computation, which is the property that makes memoizing a
    // pure function safe: the cached answer can never drift from what the
    // function itself would say.
    for (int key = 0; key < 20; ++key) {
        EXPECT_EQ(memoized(key), cube(key));
    }
    EXPECT_EQ(memoized.size(), 20U);
}

TEST(MemoizerTest, DistinctKeysAreTrackedIndependently) {
    auto negate = [](int key) { return -key; };
    Memoizer<int, int, decltype(negate)> memoized(negate);

    memoized(1);
    memoized(2);
    memoized(1);  // hit
    memoized(3);
    memoized(2);  // hit

    EXPECT_EQ(memoized.miss_count(), 3U);
    EXPECT_EQ(memoized.hit_count(), 2U);
}

}  // namespace
}  // namespace ch07
