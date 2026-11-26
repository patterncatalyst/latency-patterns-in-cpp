#pragma once

#include <filesystem>
#include <vector>

#include "ch09/audit_record.hpp"

namespace ch09 {

// The "before": one parcel, one durable write, on the hot path's own
// calling thread. The file is opened O_DSYNC, so write_record() does not
// return until the record is flushed to stable storage; the hot path pays
// that cost, every parcel, directly in its own scan-to-divert time.
class SyncAuditWriter {
public:
    explicit SyncAuditWriter(const std::filesystem::path& path);
    ~SyncAuditWriter();

    SyncAuditWriter(const SyncAuditWriter&) = delete;
    SyncAuditWriter& operator=(const SyncAuditWriter&) = delete;
    SyncAuditWriter(SyncAuditWriter&&) = delete;
    SyncAuditWriter& operator=(SyncAuditWriter&&) = delete;

    // Throws std::runtime_error on a short or failed write.
    void write_record(const AuditRecord& record);

    [[nodiscard]] std::size_t written_count() const noexcept { return written_count_; }

private:
    int fd_;
    std::size_t written_count_ = 0;
};

// Reads every fixed-size AuditRecord back out of a file written by
// SyncAuditWriter or a BatchAuditWriter (both use the identical record
// layout), for the chapter's own correctness check: the sync lane and the
// off-path lane must agree on the same set of parcel_ids.
std::vector<AuditRecord> read_all_records(const std::filesystem::path& path);

}  // namespace ch09
