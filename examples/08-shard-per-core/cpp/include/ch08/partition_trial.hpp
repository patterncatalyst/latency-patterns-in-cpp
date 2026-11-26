#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "ch08/shared_lock_tracker.hpp"
#include "ch08/sharded_tracker_pool.hpp"
#include "ch08/tracked_state.hpp"

namespace ch08 {

// One trial's result: a batch-relative completion timestamp per event (what
// the chapter's latency distribution is built from) and the batch's total
// clear time (what its throughput is built from). Both trials below share
// this shape so they are measured and reported identically.
struct PartitionTrialResult {
    std::vector<double> completion_us;
    double total_wall_time_us = 0.0;
};

// Splits `events` into worker_count contiguous slices and runs one
// std::jthread per slice, every slice calling the same tracker's observe()
// directly. Every core serializes through `tracker`'s one mutex regardless
// of which slice, or which barcode, it is working on: this is the baseline
// every other lane in this chapter is measured against.
PartitionTrialResult run_shared_lock_trial(const std::vector<Barcode>& events, SharedLockTracker& tracker,
                                            std::size_t worker_count);

// Routes every event through `router` (hash_route() for direct hash
// routing, or a RoutingTable's route() for the indirection) to decide which
// of `pool`'s shards applies it, then submits all of them from the calling
// thread and waits for every shard to drain. completion_us[i] is stamped by
// whichever shard's worker actually applied events[i], so a shard whose
// queue backs up under skew shows that backlog as a later completion time
// for everything still waiting behind it.
PartitionTrialResult run_sharded_trial(const std::vector<Barcode>& events, ShardedTrackerPool& pool,
                                        const std::function<std::size_t(Barcode)>& router);

}  // namespace ch08
