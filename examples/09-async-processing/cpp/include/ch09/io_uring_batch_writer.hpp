#pragma once

// Built only when CH09_HAVE_IO_URING is defined (see CMakeLists.txt: Conan
// resolves liburing/2.15 and the option is turned off automatically if the
// package cannot be found, falling back to BlockingBatchAuditWriter's
// epoll-driven baseline; see "How the code works" in the chapter prose).
#ifdef CH09_HAVE_IO_URING

#include <cstdint>
#include <filesystem>

#include "ch09/batch_audit_writer.hpp"

struct io_uring;  // NOLINT(readability-identifier-naming) - liburing's own type name

namespace ch09 {

// The faster path: the same one-flush-per-batch contract as
// BlockingBatchAuditWriter, but the actual write is submitted through
// io_uring instead of a blocking writev() syscall. The background consumer
// still waits for the batch's completion before returning from
// flush_batch() (a batch has to be durable before the consumer reports it
// written), so this does not change what the consumer waits for; it changes
// how the kernel is asked to do the write, one submission queue entry per
// record in the batch instead of one syscall per record, reaped as
// completions off one io_uring instance.
class IoUringBatchAuditWriter final : public BatchAuditWriter {
public:
    explicit IoUringBatchAuditWriter(const std::filesystem::path& path, unsigned queue_depth = 256);
    ~IoUringBatchAuditWriter() override;

    IoUringBatchAuditWriter(const IoUringBatchAuditWriter&) = delete;
    IoUringBatchAuditWriter& operator=(const IoUringBatchAuditWriter&) = delete;
    IoUringBatchAuditWriter(IoUringBatchAuditWriter&&) = delete;
    IoUringBatchAuditWriter& operator=(IoUringBatchAuditWriter&&) = delete;

    void flush_batch(std::span<const AuditRecord> batch) override;
    [[nodiscard]] std::size_t flush_count() const noexcept override { return flush_count_; }

private:
    int fd_;
    io_uring* ring_;
    std::size_t flush_count_ = 0;
    std::uint64_t next_offset_ = 0;
};

}  // namespace ch09

#endif  // CH09_HAVE_IO_URING
