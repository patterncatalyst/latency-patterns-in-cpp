#include "ch09/async_trial.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <unordered_set>
#include <vector>

#include "unique_test_dir.hpp"

namespace ch09 {
namespace {

using SmallTestQueue = BoundedHandoffQueue<AuditRecord, 256>;

class AsyncTrialTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = testutil::make_unique_test_dir("ch09-async-trial-test");
        sync_path_ = dir_ / "audit-sync.log";
        offpath_path_ = dir_ / "audit-offpath.log";
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    std::filesystem::path dir_;
    std::filesystem::path sync_path_;
    std::filesystem::path offpath_path_;
};

std::unordered_set<std::uint64_t> parcel_id_set(const std::filesystem::path& path) {
    std::unordered_set<std::uint64_t> ids;
    for (const AuditRecord& record : read_all_records(path)) {
        ids.insert(record.parcel_id);
    }
    return ids;
}

TEST_F(AsyncTrialTest, SyncInlineProducesOneCompletionTimePerParcel) {
    const auto parcels = generate_parcels(200, 8, 1);
    const AsyncTrialResult result = run_sync_inline_trial(parcels, sync_path_);
    EXPECT_EQ(result.completion_us.size(), parcels.size());
    EXPECT_EQ(result.shed_count, 0U);
    EXPECT_EQ(parcel_id_set(sync_path_).size(), parcels.size());
}

TEST_F(AsyncTrialTest, OffPathAgreesWithSyncOnTheSetOfRecords) {
    const auto parcels = generate_parcels(300, 8, 2);
    const AsyncTrialResult sync_result = run_sync_inline_trial(parcels, sync_path_);

    BlockingBatchAuditWriter writer(offpath_path_);
    const AsyncTrialResult offpath_result = run_offpath_trial<SmallTestQueue>(
        parcels, writer, /*batch_capacity=*/16, std::chrono::milliseconds(5), std::chrono::milliseconds(50));

    EXPECT_EQ(offpath_result.completion_us.size(), parcels.size());
    EXPECT_EQ(offpath_result.shed_count, 0U);  // generous max_wait for this size batch: nothing should shed
    EXPECT_EQ(sync_result.shed_count, 0U);

    const auto sync_ids = parcel_id_set(sync_path_);
    const auto offpath_ids = parcel_id_set(offpath_path_);
    EXPECT_EQ(sync_ids, offpath_ids);
    EXPECT_EQ(offpath_ids.size(), parcels.size());
}

// A median-latency comparison between the two lanes is deliberately not a
// ctest assertion: CTest's default preset runs every test in this repo
// concurrently (jobs=0), and under that load a wall-clock comparison this
// small (400 parcels) is noisy enough to flip. The measured comparison
// belongs to demo.sh and results/async-processing.txt, run alone, not to a
// unit test sharing the host with 170-odd other tests (see "Measurement
// discipline" in CLAUDE.md: verify the effect in the actual measurement,
// not in a test that only coincidentally measures it).

}  // namespace
}  // namespace ch09
