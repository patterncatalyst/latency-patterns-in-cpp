#include "ch08/shared_lock_tracker.hpp"

namespace ch08 {

void SharedLockTracker::observe(Barcode key, double timestamp_us) {
    const std::scoped_lock lock(mutex_);
    TrackedState& state = entries_[key];
    ++state.seen_count;
    state.last_seen_us = timestamp_us;
}

TrackedState SharedLockTracker::snapshot(Barcode key) const {
    const std::scoped_lock lock(mutex_);
    const auto it = entries_.find(key);
    return it == entries_.end() ? TrackedState{} : it->second;
}

std::size_t SharedLockTracker::entry_count() const {
    const std::scoped_lock lock(mutex_);
    return entries_.size();
}

}  // namespace ch08
