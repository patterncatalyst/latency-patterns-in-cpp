#pragma once

#include <mutex>
#include <unordered_map>

#include "ch08/tracked_state.hpp"

namespace ch08 {

// The chapter's baseline: one in-flight parcel tracker, one map, one mutex.
// Every core that scans a parcel calls observe() on the same instance, so
// every core contends for the same lock no matter which barcode it saw (see
// "The shared-lock baseline and why it contends"). Correct and simple; the
// rest of this chapter measures what that one lock costs under load.
class SharedLockTracker {
public:
    void observe(Barcode key, double timestamp_us);

    [[nodiscard]] TrackedState snapshot(Barcode key) const;

    // The number of distinct barcodes currently held. O(1) under the same
    // lock every observe() takes; contrast ShardedTrackerPool::total_entry_count,
    // which has no single lock to read a global count under (see "Trade-offs").
    [[nodiscard]] std::size_t entry_count() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<Barcode, TrackedState> entries_;
};

}  // namespace ch08
