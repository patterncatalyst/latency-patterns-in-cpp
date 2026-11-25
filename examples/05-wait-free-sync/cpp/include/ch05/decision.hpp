#pragma once

#include <chrono>
#include <cstdint>

namespace ch05 {

// One scan decision handed from the scan thread to the divert thread: which
// chute a parcel resolves to, plus the moment it entered the queue. The
// divert thread subtracts `enqueued_at` from its own receive time to get
// the handoff latency this chapter measures (see CLAUDE.md, "The spine":
// scan-to-divert). Trivially copyable, so both queue implementations can
// move it by value without a second allocation.
struct Decision {
    std::uint64_t parcel_id = 0;
    int chute = -1;
    std::chrono::steady_clock::time_point enqueued_at;
};

}  // namespace ch05
