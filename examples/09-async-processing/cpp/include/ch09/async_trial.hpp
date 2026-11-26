#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "ch09/batch_audit_writer.hpp"
#include "ch09/buffer_pool.hpp"
#include "ch09/event_loop_consumer.hpp"
#include "ch09/hot_path.hpp"
#include "ch09/parcel.hpp"
#include "ch09/sync_audit_writer.hpp"
#include "ch09/telemetry_sink.hpp"

namespace ch09 {

struct AsyncTrialResult {
    std::vector<double> completion_us;  // one per parcel, hot-path scan-to-divert time
    std::uint64_t shed_count = 0;
    std::uint64_t high_water_mark = 0;
    double total_wall_time_us = 0.0;  // hot-path wall time only, not the background consumer's drain tail
};

// Lane "before": every parcel's scan-to-divert time includes a synchronous,
// durable write on the hot path's own thread.
AsyncTrialResult run_sync_inline_trial(const std::vector<Parcel>& parcels, const std::filesystem::path& audit_path);

// Lane "after": every parcel's scan-to-divert time is the hot path's own
// enqueue cost (plus a bounded backpressure wait on the rare full-queue
// case); the durable write happens later, on EventLoopConsumer's thread,
// batched. Templated on the queue type so the same trial runner drives both
// the headline (HandoffQueue) and backpressure-demo (SmallHandoffQueue)
// lanes.
template <typename QueueT>
AsyncTrialResult run_offpath_trial(const std::vector<Parcel>& parcels, BatchAuditWriter& writer,
                                    std::size_t batch_capacity, std::chrono::milliseconds max_batch_delay,
                                    std::chrono::microseconds max_wait, TelemetrySink* telemetry = nullptr) {
    AsyncTrialResult result;
    result.completion_us.reserve(parcels.size());

    QueueT queue;
    BufferPool<AuditRecord> pool(/*buffer_count=*/4, batch_capacity);
    EventLoopConsumer<QueueT> consumer(queue, writer, pool, batch_capacity, max_batch_delay, telemetry);

    const auto trial_start = Clock::now();
    for (const Parcel& parcel : parcels) {
        const OffPathOutcome outcome = process_parcel_offpath(parcel, queue, max_wait);
        result.completion_us.push_back(outcome.completion_us);
        if (outcome.shed) {
            ++result.shed_count;
        }
    }
    result.total_wall_time_us = std::chrono::duration<double, std::micro>(Clock::now() - trial_start).count();
    result.high_water_mark = queue.high_water_mark();
    // consumer (and then pool, then queue) destruct here, in that order: the
    // consumer's destructor joins its background thread, which runs a final
    // drain-and-flush loop first, so every record still in the queue when
    // the hot-path loop above finished is durable before this function
    // returns, even though that flush tail is not part of the hot-path
    // timing captured above.
    return result;
}

}  // namespace ch09
