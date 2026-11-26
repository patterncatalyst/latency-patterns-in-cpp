#include "ch09/batch_audit_writer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <vector>

#include "ch09/sync_audit_writer.hpp"
#include "unique_test_dir.hpp"

namespace ch09 {
namespace {

class BlockingBatchAuditWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = testutil::make_unique_test_dir("ch09-batch-audit-writer-test");
        path_ = dir_ / "audit.log";
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    std::filesystem::path dir_;
    std::filesystem::path path_;
};

TEST_F(BlockingBatchAuditWriterTest, FlushesEveryRecordInTheBatch) {
    std::vector<AuditRecord> batch(20);
    for (std::uint64_t i = 0; i < batch.size(); ++i) {
        batch[i].parcel_id = i;
        batch[i].chute_id = static_cast<std::uint32_t>(i % 5);
    }
    {
        BlockingBatchAuditWriter writer(path_);
        writer.flush_batch(batch);
        EXPECT_EQ(writer.flush_count(), 1U);
    }
    const auto records = read_all_records(path_);
    ASSERT_EQ(records.size(), batch.size());
    for (std::size_t i = 0; i < batch.size(); ++i) {
        EXPECT_EQ(records[i].parcel_id, batch[i].parcel_id);
        EXPECT_EQ(records[i].chute_id, batch[i].chute_id);
    }
}

TEST_F(BlockingBatchAuditWriterTest, MultipleBatchesAppendInOrder) {
    BlockingBatchAuditWriter writer(path_);
    std::vector<AuditRecord> first(5);
    std::vector<AuditRecord> second(5);
    for (std::uint64_t i = 0; i < 5; ++i) {
        first[i].parcel_id = i;
        second[i].parcel_id = i + 5;
    }
    writer.flush_batch(first);
    writer.flush_batch(second);
    EXPECT_EQ(writer.flush_count(), 2U);

    const auto records = read_all_records(path_);
    ASSERT_EQ(records.size(), 10U);
    for (std::uint64_t i = 0; i < 10; ++i) {
        EXPECT_EQ(records[i].parcel_id, i);
    }
}

TEST_F(BlockingBatchAuditWriterTest, EmptyBatchIsANoOp) {
    BlockingBatchAuditWriter writer(path_);
    writer.flush_batch({});
    EXPECT_EQ(writer.flush_count(), 0U);
}

}  // namespace
}  // namespace ch09
