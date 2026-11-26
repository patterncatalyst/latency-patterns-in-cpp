#include "ch09/hot_path.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string_view>

#include "unique_test_dir.hpp"

namespace ch09 {
namespace {

TEST(HotPathTest, BuildAuditRecordCarriesParcelIdAndChute) {
    Parcel parcel;
    parcel.parcel_id = 42;
    parcel.chute_id = 7;
    const AuditRecord record = build_audit_record(parcel);
    EXPECT_EQ(record.parcel_id, 42U);
    EXPECT_EQ(record.chute_id, 7U);
    const std::string_view label(record.label.data());
    EXPECT_NE(label.find("42"), std::string_view::npos);
    EXPECT_NE(label.find('7'), std::string_view::npos);
}

TEST(HotPathTest, ProcessParcelSyncWritesExactlyOneRecordAndReturnsAPositiveTime) {
    const auto dir = testutil::make_unique_test_dir("ch09-hot-path-test");
    const auto path = dir / "audit.log";
    {
        SyncAuditWriter writer(path);
        Parcel parcel;
        parcel.parcel_id = 1;
        parcel.chute_id = 2;
        const double completion_us = process_parcel_sync(parcel, writer);
        EXPECT_GE(completion_us, 0.0);
        EXPECT_EQ(writer.written_count(), 1U);
    }
    const auto records = read_all_records(path);
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records.front().parcel_id, 1U);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

}  // namespace
}  // namespace ch09
