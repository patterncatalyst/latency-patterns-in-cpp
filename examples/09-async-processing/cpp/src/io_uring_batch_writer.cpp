#include "ch09/io_uring_batch_writer.hpp"

#ifdef CH09_HAVE_IO_URING

#include <fcntl.h>
#include <unistd.h>

// liburing.h is a C header not written against this project's warning set
// (-Wconversion, -Wold-style-cast via clang-tidy's modernize checks, etc.);
// the suppression stays scoped to this one include, never to ch09's own
// code below it.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wcast-align"
#include <liburing.h>
#pragma clang diagnostic pop

#include <sys/uio.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace ch09 {

namespace {

[[noreturn]] void throw_errno(const char* what, int err) {
    throw std::runtime_error(std::string(what) + ": " + std::strerror(err < 0 ? -err : err));
}

}  // namespace

IoUringBatchAuditWriter::IoUringBatchAuditWriter(const std::filesystem::path& path, unsigned queue_depth)
    : fd_(::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_DSYNC, 0600)),  // NOLINT(cppcoreguidelines-pro-type-vararg)
      ring_(new io_uring{}) {
    if (fd_ < 0) {
        delete ring_;
        throw_errno("IoUringBatchAuditWriter: open() failed", errno);
    }
    const int result = io_uring_queue_init(queue_depth, ring_, 0);
    if (result < 0) {
        ::close(fd_);
        delete ring_;
        throw_errno("IoUringBatchAuditWriter: io_uring_queue_init() failed", result);
    }
}

IoUringBatchAuditWriter::~IoUringBatchAuditWriter() {
    io_uring_queue_exit(ring_);
    delete ring_;
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void IoUringBatchAuditWriter::flush_batch(std::span<const AuditRecord> batch) {
    if (batch.empty()) {
        return;
    }
    // One SQE for the whole batch (io_uring's own scatter-gather write,
    // io_uring_prep_writev), not one SQE per record: on an O_DSYNC fd, every
    // individual write operation is its own durability point, so one SQE
    // per record would still pay one flush per record, the exact cost this
    // chapter moves off the hot path by batching. One writev-equivalent SQE
    // for the whole batch is what turns "N records" into "one flush," the
    // same amortization BlockingBatchAuditWriter gets from one writev()
    // syscall; io_uring's contribution here is submitting that one request
    // without a blocking syscall on this thread while it is in flight.
    std::vector<iovec> iovecs;
    iovecs.reserve(batch.size());
    for (const AuditRecord& record : batch) {
        iovec iov{};
        iov.iov_base = const_cast<AuditRecord*>(&record);  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        iov.iov_len = sizeof(record);
        iovecs.push_back(iov);
    }

    io_uring_sqe* sqe = io_uring_get_sqe(ring_);
    if (sqe == nullptr) {
        throw std::runtime_error("IoUringBatchAuditWriter: io_uring_get_sqe() returned null");
    }
    io_uring_prep_writev(sqe, fd_, iovecs.data(), static_cast<unsigned>(iovecs.size()), next_offset_);

    const int submitted = io_uring_submit(ring_);
    if (submitted < 0) {
        throw_errno("IoUringBatchAuditWriter: io_uring_submit() failed", submitted);
    }
    io_uring_cqe* cqe = nullptr;
    const int wait_result = io_uring_wait_cqe(ring_, &cqe);
    if (wait_result < 0) {
        throw_errno("IoUringBatchAuditWriter: io_uring_wait_cqe() failed", wait_result);
    }
    if (cqe->res < 0) {
        const int cqe_err = cqe->res;
        io_uring_cqe_seen(ring_, cqe);
        throw_errno("IoUringBatchAuditWriter: write completion failed", cqe_err);
    }
    const auto bytes_written = static_cast<std::uint64_t>(cqe->res);
    io_uring_cqe_seen(ring_, cqe);
    next_offset_ += bytes_written;
    ++flush_count_;
}

}  // namespace ch09

#endif  // CH09_HAVE_IO_URING
