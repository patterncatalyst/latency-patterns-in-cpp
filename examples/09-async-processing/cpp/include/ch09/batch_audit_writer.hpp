#pragma once

#include <filesystem>
#include <span>

#include "ch09/audit_record.hpp"

namespace ch09 {

// What the background consumer calls once per drained batch: one durable
// flush for up to a whole batch of records, instead of one per record. This
// is the interface both multiplexing backends implement (see
// BlockingBatchAuditWriter for the portable epoll-driven baseline and
// IoUringBatchAuditWriter for the faster path); the event loop that calls
// it does not care which one it is handed.
class BatchAuditWriter {
public:
    BatchAuditWriter() = default;
    virtual ~BatchAuditWriter() = default;

    BatchAuditWriter(const BatchAuditWriter&) = delete;
    BatchAuditWriter& operator=(const BatchAuditWriter&) = delete;
    BatchAuditWriter(BatchAuditWriter&&) = delete;
    BatchAuditWriter& operator=(BatchAuditWriter&&) = delete;

    // Writes every record in batch as one durable unit (one flush for the
    // whole batch, not one per record) and returns once every byte is on
    // stable storage. Called from the background consumer thread only.
    virtual void flush_batch(std::span<const AuditRecord> batch) = 0;

    [[nodiscard]] virtual std::size_t flush_count() const noexcept = 0;
};

// The portable baseline: a file opened O_DSYNC, one writev() per batch.
// Every record in the batch still lands durably, but the flush cost (the
// expensive part of a durable write) is paid once per batch rather than
// once per record.
class BlockingBatchAuditWriter final : public BatchAuditWriter {
public:
    explicit BlockingBatchAuditWriter(const std::filesystem::path& path);
    ~BlockingBatchAuditWriter() override;

    BlockingBatchAuditWriter(const BlockingBatchAuditWriter&) = delete;
    BlockingBatchAuditWriter& operator=(const BlockingBatchAuditWriter&) = delete;
    BlockingBatchAuditWriter(BlockingBatchAuditWriter&&) = delete;
    BlockingBatchAuditWriter& operator=(BlockingBatchAuditWriter&&) = delete;

    void flush_batch(std::span<const AuditRecord> batch) override;
    [[nodiscard]] std::size_t flush_count() const noexcept override { return flush_count_; }

private:
    int fd_;
    std::size_t flush_count_ = 0;
};

}  // namespace ch09
