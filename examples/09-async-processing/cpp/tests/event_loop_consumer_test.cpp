#include "ch09/event_loop_consumer.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <unordered_set>

#include "ch09/batch_audit_writer.hpp"
#include "ch09/buffer_pool.hpp"
#include "ch09/bounded_handoff_queue.hpp"
#include "ch09/sync_audit_writer.hpp"
#include "unique_test_dir.hpp"

namespace ch09 {
namespace {

using TestQueue = BoundedHandoffQueue<AuditRecord, 256>;

class EventLoopConsumerTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = testutil::make_unique_test_dir("ch09-event-loop-consumer-test");
        path_ = dir_ / "audit.log";
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    std::filesystem::path dir_;
    std::filesystem::path path_;
};

TEST_F(EventLoopConsumerTest, EveryPushedRecordIsFlushedExactlyOnce) {
    BlockingBatchAuditWriter writer(path_);
    BufferPool<AuditRecord> pool(4, 16);
    TestQueue queue;
    constexpr std::size_t item_count = 500;
    {
        EventLoopConsumer<TestQueue> consumer(queue, writer, pool, /*batch_capacity=*/16,
                                               std::chrono::milliseconds(5));
        for (std::uint64_t i = 0; i < item_count; ++i) {
            AuditRecord record;
            record.parcel_id = i;
            ASSERT_TRUE(queue.push_with_backpressure(record, std::chrono::milliseconds(200)));
        }
        // consumer destructs here: request_stop() wakes the event loop,
        // which runs its final drain-and-flush loop before the thread
        // joins, so every record pushed above is durable by the time this
        // scope ends.
    }
    const auto records = read_all_records(path_);
    std::unordered_set<std::uint64_t> seen;
    for (const AuditRecord& record : records) {
        EXPECT_TRUE(seen.insert(record.parcel_id).second) << "duplicate parcel_id " << record.parcel_id;
    }
    EXPECT_EQ(seen.size(), item_count);
    for (std::uint64_t i = 0; i < item_count; ++i) {
        EXPECT_TRUE(seen.contains(i)) << "missing parcel_id " << i;
    }
}

TEST_F(EventLoopConsumerTest, TimerAloneFlushesAPartialBatch) {
    BlockingBatchAuditWriter writer(path_);
    BufferPool<AuditRecord> pool(2, 64);
    TestQueue queue;
    {
        // batch_capacity (64) is never reached (only 3 records pushed), so
        // only the timerfd's periodic tick can trigger the flush.
        EventLoopConsumer<TestQueue> consumer(queue, writer, pool, /*batch_capacity=*/64,
                                               std::chrono::milliseconds(10));
        for (std::uint64_t i = 0; i < 3; ++i) {
            AuditRecord record;
            record.parcel_id = i;
            ASSERT_TRUE(queue.push_with_backpressure(record, std::chrono::milliseconds(200)));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_EQ(read_all_records(path_).size(), 3U);
    }
}

}  // namespace
}  // namespace ch09
