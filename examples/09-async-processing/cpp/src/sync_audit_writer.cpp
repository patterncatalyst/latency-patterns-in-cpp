#include "ch09/sync_audit_writer.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace ch09 {

namespace {

[[noreturn]] void throw_errno(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}

}  // namespace

SyncAuditWriter::SyncAuditWriter(const std::filesystem::path& path)
    : fd_(::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_DSYNC, 0600)) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (fd_ < 0) {
        throw_errno("SyncAuditWriter: open() failed");
    }
}

SyncAuditWriter::~SyncAuditWriter() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void SyncAuditWriter::write_record(const AuditRecord& record) {
    const auto* bytes = reinterpret_cast<const char*>(&record);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    std::size_t written = 0;
    while (written < sizeof(record)) {
        const ssize_t result = ::write(fd_, bytes + written, sizeof(record) - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw_errno("SyncAuditWriter: write() failed");
        }
        written += static_cast<std::size_t>(result);
    }
    ++written_count_;
}

std::vector<AuditRecord> read_all_records(const std::filesystem::path& path) {
    std::vector<AuditRecord> records;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return records;
    }
    AuditRecord record;
    while (in.read(reinterpret_cast<char*>(&record), sizeof(record))) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        records.push_back(record);
    }
    return records;
}

}  // namespace ch09
