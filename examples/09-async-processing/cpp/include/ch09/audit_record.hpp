#pragma once

#include <array>
#include <cstdint>

namespace ch09 {

// One parcel's audit/label record: the write this chapter moves off the
// hot path. Fixed size on purpose, so a sync write, a batched writev, and
// an io_uring submission all move the identical number of bytes per
// record; nothing here allocates to build one.
struct AuditRecord {
    std::uint64_t parcel_id = 0;
    std::uint32_t chute_id = 0;
    std::uint64_t decided_at_us = 0;  // wall-clock microseconds since epoch
    std::array<char, 40> label{};     // the "label print" payload, fixed width
};

}  // namespace ch09
