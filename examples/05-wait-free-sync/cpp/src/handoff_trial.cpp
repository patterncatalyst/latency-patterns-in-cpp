#include "ch05/handoff_trial.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "ch05/decision.hpp"
#include "ch05/mutex_handoff_queue.hpp"
#include "ch05/spsc_ring_queue.hpp"

namespace ch05 {

namespace {

using Clock = std::chrono::steady_clock;

// Shared bookkeeping for both lanes: given the decisions actually received,
// in receive order, fill in the correctness fields of HandoffTrialResult.
// Both lanes send parcel_id 0..item_count-1 in that order, so FIFO means
// the consumer sees that exact sequence back.
void record_received(HandoffTrialResult& result, std::uint64_t item_count,
                      const Decision& decision, std::vector<bool>& seen) {
    const Clock::time_point received_at = Clock::now();
    result.latencies_seconds.push_back(
        std::chrono::duration<double>(received_at - decision.enqueued_at).count());

    if (decision.parcel_id < item_count) {
        if (seen[decision.parcel_id]) {
            ++result.duplicate_count;
        }
        seen[decision.parcel_id] = true;
    }
    if (decision.parcel_id != result.items_received) {
        ++result.out_of_order_count;
    }
    ++result.items_received;
}

}  // namespace

HandoffTrialResult run_mutex_handoff_trial(std::uint64_t item_count) {
    HandoffTrialResult result;
    result.latencies_seconds.reserve(item_count);
    std::vector<bool> seen(item_count, false);

    MutexHandoffQueue queue;

    std::thread divert_thread([&] {
        Decision decision;
        while (queue.pop(decision)) {
            record_received(result, item_count, decision, seen);
        }
    });

    for (std::uint64_t parcel_id = 0; parcel_id < item_count; ++parcel_id) {
        Decision decision;
        decision.parcel_id = parcel_id;
        decision.chute = static_cast<int>(parcel_id % 16);
        decision.enqueued_at = Clock::now();
        queue.push(decision);
        ++result.items_sent;
    }
    queue.close();
    divert_thread.join();

    result.dropped_count = result.items_sent - result.items_received;
    return result;
}

HandoffTrialResult run_ring_handoff_trial(std::uint64_t item_count) {
    HandoffTrialResult result;
    result.latencies_seconds.reserve(item_count);
    std::vector<bool> seen(item_count, false);

    SpscRingQueue<Decision, ring_capacity> ring;
    std::atomic<std::uint64_t> full_retries{0};

    std::thread divert_thread([&] {
        Decision decision;
        while (result.items_received < item_count) {
            if (ring.try_pop(decision)) {
                record_received(result, item_count, decision, seen);
            }
            // Empty: try_pop() already returned, so just loop straight back
            // around. No sleep, no yield — the whole point of this lane is
            // that the consumer never asks the OS scheduler for anything.
        }
    });

    for (std::uint64_t parcel_id = 0; parcel_id < item_count; ++parcel_id) {
        Decision decision;
        decision.parcel_id = parcel_id;
        decision.chute = static_cast<int>(parcel_id % 16);
        decision.enqueued_at = Clock::now();
        while (!ring.try_push(decision)) {
            full_retries.fetch_add(1, std::memory_order_relaxed);
        }
        ++result.items_sent;
    }
    divert_thread.join();

    result.full_retries = full_retries.load(std::memory_order_relaxed);
    result.dropped_count = result.items_sent - result.items_received;
    return result;
}

}  // namespace ch05
