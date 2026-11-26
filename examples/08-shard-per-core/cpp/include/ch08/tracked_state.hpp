#pragma once

#include <cstdint>

namespace ch08 {

// A barcode identifies one SKU family on the sortation line. The tracker
// below holds one TrackedState per barcode currently in flight: how many
// times this run has seen it, and the batch-relative microsecond of the
// most recent sighting. Every lane in this chapter (shared-lock, sharded,
// sharded-under-skew) keys on the same Barcode and produces the same
// TrackedState shape, so the chapter's correctness check can compare them
// directly (see "How the code works").
using Barcode = std::uint64_t;

struct TrackedState {
    std::uint64_t seen_count = 0;
    double last_seen_us = 0.0;
};

}  // namespace ch08
