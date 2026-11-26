#include "ch08/sharded_tracker_pool.hpp"

#include <utility>

#include "ch08/affinity.hpp"

namespace ch08 {

namespace {

double micros_since(ShardedTrackerPool::Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(ShardedTrackerPool::Clock::now() - start).count();
}

}  // namespace

ShardedTrackerPool::ShardedTrackerPool(std::size_t shard_count, bool pin_workers_to_cores)
    : start_(Clock::now()),
      shards_(shard_count),
      queue_mutexes_(shard_count),
      not_empty_(shard_count),
      queues_(shard_count),
      submitted_counts_(shard_count, 0),
      processed_counts_(shard_count, 0) {
    workers_.reserve(shard_count);
    for (std::size_t i = 0; i < shard_count; ++i) {
        workers_.emplace_back([this, i, pin_workers_to_cores](const std::stop_token& stop_token) {
            if (pin_workers_to_cores) {
                pin_current_thread_best_effort(i);
            }
            worker_loop(i, stop_token);
        });
    }
}

ShardedTrackerPool::~ShardedTrackerPool() {
    for (std::jthread& worker : workers_) {
        worker.request_stop();
    }
    for (std::condition_variable_any& cv : not_empty_) {
        cv.notify_all();
    }
    // workers_ is the last-declared member (see sharded_tracker_pool.hpp), so
    // its jthreads are the first destroyed here; each jthread destructor
    // joins its worker before shards_, queue_mutexes_, not_empty_, and
    // queues_ above go away. A worker still parked in worker_loop's wait()
    // below wakes on a mutex and condition_variable_any that are still
    // alive when it does.
}

void ShardedTrackerPool::worker_loop(std::size_t shard_index, const std::stop_token& stop_token) {
    std::mutex& mutex = queue_mutexes_[shard_index];
    std::condition_variable_any& cv = not_empty_[shard_index];
    std::queue<Task>& queue = queues_[shard_index];
    std::unordered_map<Barcode, TrackedState>& entries = shards_[shard_index];

    while (true) {
        Task batch;
        {
            std::unique_lock lock(mutex);
            const bool have_work = cv.wait(lock, stop_token, [&] { return !queue.empty(); });
            if (!have_work) {
                return;  // stop requested with nothing left queued for this shard
            }
            batch = std::move(queue.front());
            queue.pop();
        }

        // Only this worker ever reads or writes entries: no lock, no
        // atomic, no other thread's shard ever touches it, for every item
        // in this one batch.
        for (const BatchItem& item : batch) {
            TrackedState& state = entries[item.key];
            ++state.seen_count;
            state.last_seen_us = micros_since(start_);
            if (item.completion_us_out != nullptr) {
                *item.completion_us_out = state.last_seen_us;
            }
        }

        {
            std::scoped_lock lock(mutex);
            processed_counts_[shard_index] += batch.size();
        }
        cv.notify_all();  // wake a wait_drained() call waiting on this shard
    }
}

// completion_us_out is forwarded into a BatchItem and written later, by
// whichever worker thread applies this observation, not by this function;
// clang-tidy's readability-non-const-parameter only looks at this body, not
// at BatchItem's eventual use, so it cannot see that.
// NOLINTNEXTLINE(readability-non-const-parameter)
void ShardedTrackerPool::submit(std::size_t shard_index, Barcode key, double* completion_us_out) {
    submit_batch(shard_index, {BatchItem{.key = key, .completion_us_out = completion_us_out}});
}

void ShardedTrackerPool::submit_batch(std::size_t shard_index, std::vector<BatchItem> batch) {
    if (batch.empty()) {
        return;
    }
    const std::size_t count = batch.size();
    {
        std::scoped_lock lock(queue_mutexes_[shard_index]);
        queues_[shard_index].push(std::move(batch));
        submitted_counts_[shard_index] += count;
    }
    not_empty_[shard_index].notify_one();
}

void ShardedTrackerPool::wait_drained() {
    for (std::size_t i = 0; i < queues_.size(); ++i) {
        std::unique_lock lock(queue_mutexes_[i]);
        not_empty_[i].wait(lock, [&] { return submitted_counts_[i] == processed_counts_[i]; });
    }
}

std::size_t ShardedTrackerPool::shard_size(std::size_t shard_index) const {
    return shards_.at(shard_index).size();
}

std::uint64_t ShardedTrackerPool::shard_processed_count(std::size_t shard_index) const {
    return processed_counts_.at(shard_index);
}

TrackedState ShardedTrackerPool::snapshot(Barcode key, std::size_t shard_index) const {
    const auto& entries = shards_.at(shard_index);
    const auto it = entries.find(key);
    return it == entries.end() ? TrackedState{} : it->second;
}

}  // namespace ch08
