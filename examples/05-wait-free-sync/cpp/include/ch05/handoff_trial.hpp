#pragma once

#include <cstdint>
#include <vector>

namespace ch05 {

// Ring capacity for the wait-free lane: comfortably above what a
// keeping-up consumer ever needs, so `ring_full_retries` below stays at (or
// near) zero and the measured latency reflects the handoff itself, not an
// artificially small buffer. Must be a power of two (see SpscRingQueue).
inline constexpr std::size_t ring_capacity = 4096;

// One trial's result: a latency sample per item actually delivered (in
// seconds, ready for measure::LatencyRecorder), plus the behavioral checks
// this chapter's GoogleTests also check directly: every item sent exactly
// once, received exactly once, in the order it was sent.
struct HandoffTrialResult {
    std::vector<double> latencies_seconds;
    std::uint64_t items_sent = 0;
    std::uint64_t items_received = 0;
    std::uint64_t out_of_order_count = 0;  // a received parcel_id not in increasing order
    std::uint64_t duplicate_count = 0;     // a parcel_id received more than once
    std::uint64_t dropped_count = 0;       // items_sent - items_received
    std::uint64_t full_retries = 0;        // ring lane only: try_push() calls that saw a full ring
};

// Runs item_count decisions through a MutexHandoffQueue: one producer
// thread (the scan stage) pushing, one consumer thread (the divert stage)
// draining via the blocking pop(), on the calling thread's own two spawned
// threads. Both threads run flat out, no pacing, which is the contention
// this chapter measures: every push and every pop takes the same lock.
HandoffTrialResult run_mutex_handoff_trial(std::uint64_t item_count);

// Runs the identical item_count decisions through an SpscRingQueue: one
// producer thread calling try_push() in a tight retry loop, one consumer
// thread calling try_pop() in a tight spin loop. Neither thread ever blocks
// on the other through the OS scheduler; the only thing either one waits on
// is its own next atomic load coming back.
HandoffTrialResult run_ring_handoff_trial(std::uint64_t item_count);

}  // namespace ch05
