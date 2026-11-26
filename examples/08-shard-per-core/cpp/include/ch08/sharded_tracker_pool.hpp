#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ch08/tracked_state.hpp"

namespace ch08 {

// Shard-per-core, for real: shard_count independent {map, queue, mutex,
// condition_variable} groups, one std::jthread worker per shard, and
// nothing shared across shards at all, not even a lock. A router (see
// routing.hpp) decides which shard a barcode's observation belongs to;
// submit() enqueues it there and returns immediately, and that shard's own
// worker thread is the only thread that ever reads or writes that shard's
// map. Compare SharedLockTracker, where every core serializes through the
// same one mutex regardless of which barcode it saw.
//
// Usage: submit() every observation, call wait_drained() once, then read
// shard_size()/shard_processed_count()/snapshot(). Reading any of those
// before wait_drained() returns is a race; nothing stops you from calling
// them while workers are still live, but the maps they read are owned by
// threads that may be mutating them at that moment.
class ShardedTrackerPool {
public:
    using Clock = std::chrono::steady_clock;

    explicit ShardedTrackerPool(std::size_t shard_count, bool pin_workers_to_cores = false);
    ~ShardedTrackerPool();

    ShardedTrackerPool(const ShardedTrackerPool&) = delete;
    ShardedTrackerPool& operator=(const ShardedTrackerPool&) = delete;
    ShardedTrackerPool(ShardedTrackerPool&&) = delete;
    ShardedTrackerPool& operator=(ShardedTrackerPool&&) = delete;

    // One queued observation: a barcode and where to stamp its completion
    // time once the owning shard applies it (null if the caller does not
    // need it).
    struct BatchItem {
        Barcode key;
        double* completion_us_out;
    };

    // Enqueues one observation for shard_index's owned map. Returns once
    // queued, not once applied: the apply happens later, on shard_index's
    // own worker thread. If completion_us_out is non-null, that worker
    // stamps it with the microsecond offset (relative to start(), this
    // pool's construction time) at which it applied this observation, so a
    // shard whose queue backs up under load shows that backlog directly as
    // a later completion_us for the observations stuck behind it.
    void submit(std::size_t shard_index, Barcode key, double* completion_us_out);

    // Enqueues every item in `batch` as a single handoff to shard_index: one
    // lock, one push, one notify for the whole batch, rather than paying
    // that cost once per item. The owning worker still applies every item
    // in order and stamps each one's completion_us_out individually; what
    // changes is the cost of getting the work there in the first place. A
    // known batch (every lane this book measures) should call this instead
    // of submit() in a loop (see "How the code works" for why the
    // difference is not a rounding error).
    void submit_batch(std::size_t shard_index, std::vector<BatchItem> batch);

    // Blocks until every shard has applied everything submit() has handed
    // it so far. Call once after the last submit() and before reading any
    // shard's state.
    void wait_drained();

    [[nodiscard]] Clock::time_point start() const noexcept { return start_; }
    [[nodiscard]] std::size_t shard_count() const noexcept { return shards_.size(); }

    // Distinct barcodes currently held by one shard. Call only after
    // wait_drained().
    [[nodiscard]] std::size_t shard_size(std::size_t shard_index) const;

    // Total observations one shard's worker has applied so far: the
    // per-shard load this chapter's skew measurement reads. Call only after
    // wait_drained() for a stable, final count.
    [[nodiscard]] std::uint64_t shard_processed_count(std::size_t shard_index) const;

    // Call only after wait_drained(). shard_index must be the shard the
    // caller's router actually sends `key` to; this type does no routing of
    // its own and does not search other shards for the key.
    [[nodiscard]] TrackedState snapshot(Barcode key, std::size_t shard_index) const;

private:
    using Task = std::vector<BatchItem>;

    void worker_loop(std::size_t shard_index, const std::stop_token& stop_token);

    Clock::time_point start_;

    // Per-shard state, index-aligned, sized once in the constructor and
    // never resized again. Only shards_[i] is ever touched by more than the
    // one worker that owns it, and only under queue_mutexes_[i]; the map
    // itself is touched only by that one worker, lock-free by construction.
    std::vector<std::unordered_map<Barcode, TrackedState>> shards_;
    mutable std::vector<std::mutex> queue_mutexes_;
    std::vector<std::condition_variable_any> not_empty_;
    std::vector<std::queue<Task>> queues_;
    std::vector<std::uint64_t> submitted_counts_;  // guarded by queue_mutexes_[i]
    std::vector<std::uint64_t> processed_counts_;  // guarded by queue_mutexes_[i]

    // Declaration order is destruction order, reversed (see
    // examples/06-exploiting-concurrency/cpp/include/ch06/thread_pool.hpp):
    // workers_ must come last so its jthreads join, and stop running, before
    // shards_, queue_mutexes_, not_empty_, and queues_ above are destroyed.
    // Putting it anywhere else is the data race and heap-use-after-free this
    // book's concurrency chapters specifically flag.
    std::vector<std::jthread> workers_;
};

}  // namespace ch08
