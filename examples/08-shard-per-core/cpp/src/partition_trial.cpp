#include "ch08/partition_trial.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace ch08 {

namespace {

using Clock = std::chrono::steady_clock;

double micros_since(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

}  // namespace

PartitionTrialResult run_shared_lock_trial(const std::vector<Barcode>& events, SharedLockTracker& tracker,
                                            std::size_t worker_count) {
    PartitionTrialResult result;
    const std::size_t n = events.size();
    result.completion_us.resize(n);

    const std::size_t chunk = (n + worker_count - 1) / worker_count;
    const Clock::time_point start = Clock::now();
    {
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);
        for (std::size_t w = 0; w < worker_count; ++w) {
            const std::size_t begin = w * chunk;
            const std::size_t end = std::min(n, begin + chunk);
            if (begin >= end) {
                continue;
            }
            workers.emplace_back([&, begin, end] {
                for (std::size_t i = begin; i < end; ++i) {
                    tracker.observe(events[i], micros_since(start));
                    result.completion_us[i] = micros_since(start);
                }
            });
        }
    }  // every jthread joins here; each touched only its own [begin, end) slice
    result.total_wall_time_us = micros_since(start);
    return result;
}

PartitionTrialResult run_sharded_trial(const std::vector<Barcode>& events, ShardedTrackerPool& pool,
                                        const std::function<std::size_t(Barcode)>& router) {
    PartitionTrialResult result;
    const std::size_t n = events.size();
    result.completion_us.resize(n);

    // Group every event by its routed shard up front and hand each shard
    // its whole slice as one submit_batch() call: one lock, one push, one
    // notify per shard, not per event. A naive per-event submit() here pays
    // a queue push and a condition-variable wake for every single
    // observation, which this book measured as costing more than the one
    // mutex it was meant to replace (see "How the code works").
    std::vector<std::vector<ShardedTrackerPool::BatchItem>> batches(pool.shard_count());
    for (std::size_t i = 0; i < n; ++i) {
        batches.at(router(events[i]))
            .push_back(ShardedTrackerPool::BatchItem{.key = events[i], .completion_us_out = &result.completion_us[i]});
    }
    for (std::size_t shard = 0; shard < batches.size(); ++shard) {
        pool.submit_batch(shard, std::move(batches[shard]));
    }

    pool.wait_drained();
    result.total_wall_time_us = micros_since(pool.start());
    return result;
}

}  // namespace ch08
