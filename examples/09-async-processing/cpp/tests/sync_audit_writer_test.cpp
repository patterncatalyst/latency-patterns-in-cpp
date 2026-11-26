#include "ch09/sync_audit_writer.hpp"

#include <gtest/gtest.h>

#include <filesystem>

#include "unique_test_dir.hpp"

namespace ch09 {
namespace {

class SyncAuditWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = testutil::make_unique_test_dir("ch09-sync-audit-writer-test");
        path_ = dir_ / "audit.log";
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    std::filesystem::path dir_;
    std::filesystem::path path_;
};

TEST_F(SyncAuditWriterTest, WritesAreReadableBackInOrder) {
    {
        SyncAuditWriter writer(path_);
        for (std::uint64_t i = 0; i < 10; ++i) {
            AuditRecord record;
            record.parcel_id = i;
            record.chute_id = static_cast<std::uint32_t>(i % 4);
            writer.write_record(record);
        }
        EXPECT_EQ(writer.written_count(), 10U);
    }
    const auto records = read_all_records(path_);
    ASSERT_EQ(records.size(), 10U);
    for (std::uint64_t i = 0; i < 10; ++i) {
        EXPECT_EQ(records[i].parcel_id, i);
        EXPECT_EQ(records[i].chute_id, i % 4);
    }
}

TEST_F(SyncAuditWriterTest, ReadAllRecordsOnAMissingFileReturnsEmpty) {
    const auto records = read_all_records(std::filesystem::temp_directory_path() / "ch09_does_not_exist.log");
    EXPECT_TRUE(records.empty());
}

}  // namespace
}  // namespace ch09
