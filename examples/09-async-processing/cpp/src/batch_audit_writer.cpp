#include "ch09/batch_audit_writer.hpp"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace ch09 {

namespace {

[[noreturn]] void throw_errno(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}

}  // namespace

BlockingBatchAuditWriter::BlockingBatchAuditWriter(const std::filesystem::path& path)
    : fd_(::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_DSYNC, 0600)) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (fd_ < 0) {
        throw_errno("BlockingBatchAuditWriter: open() failed");
    }
}

BlockingBatchAuditWriter::~BlockingBatchAuditWriter() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void BlockingBatchAuditWriter::flush_batch(std::span<const AuditRecord> batch) {
    if (batch.empty()) {
        return;
    }
    std::vector<iovec> iovecs;
    iovecs.reserve(batch.size());
    for (const AuditRecord& record : batch) {
        iovec iov{};
        iov.iov_base = const_cast<AuditRecord*>(&record);  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        iov.iov_len = sizeof(record);
        iovecs.push_back(iov);
    }
    std::size_t iov_offset = 0;
    while (iov_offset < iovecs.size()) {
        const auto remaining = static_cast<int>(iovecs.size() - iov_offset);
        const ssize_t result = ::writev(fd_, iovecs.data() + iov_offset, remaining);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw_errno("BlockingBatchAuditWriter: writev() failed");
        }
        // writev() on a regular file either writes everything requested or
        // fails; a partial-iovec retry loop is not needed in practice here,
        // but advancing past fully-consumed records keeps this correct even
        // if the kernel ever returns a short write.
        auto consumed = static_cast<std::size_t>(result);
        while (consumed > 0 && iov_offset < iovecs.size()) {
            if (consumed < iovecs[iov_offset].iov_len) {
                iovecs[iov_offset].iov_base = static_cast<char*>(iovecs[iov_offset].iov_base) + consumed;
                iovecs[iov_offset].iov_len -= consumed;
                consumed = 0;
            } else {
                consumed -= iovecs[iov_offset].iov_len;
                ++iov_offset;
            }
        }
    }
    ++flush_count_;
}

}  // namespace ch09
