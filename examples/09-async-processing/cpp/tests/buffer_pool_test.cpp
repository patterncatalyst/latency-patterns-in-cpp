#include "ch09/buffer_pool.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace ch09 {
namespace {

TEST(BufferPoolTest, AcquireReleaseReusesABuffer) {
    BufferPool<int> pool(2, 8);
    auto buf = pool.acquire();
    ASSERT_NE(buf, nullptr);
    buf->push_back(1);
    buf->push_back(2);
    pool.release(std::move(buf));

    auto reused = pool.acquire();
    EXPECT_TRUE(reused->empty());  // release() clears it
    EXPECT_GE(reused->capacity(), 8U);
}

TEST(BufferPoolTest, GrowsPastProvisionedCountRatherThanBlocking) {
    BufferPool<int> pool(1, 4);
    auto first = pool.acquire();
    auto second = pool.acquire();  // pool had only 1 provisioned buffer
    EXPECT_EQ(pool.allocations_beyond_pool(), 1U);
    pool.release(std::move(first));
    pool.release(std::move(second));
}

TEST(BufferPoolTest, StaysWithinProvisionedCountUnderNormalAcquireRelease) {
    BufferPool<std::uint64_t> pool(3, 16);
    for (int i = 0; i < 100; ++i) {
        auto buf = pool.acquire();
        buf->push_back(static_cast<std::uint64_t>(i));
        pool.release(std::move(buf));
    }
    EXPECT_EQ(pool.allocations_beyond_pool(), 0U);
}

}  // namespace
}  // namespace ch09
