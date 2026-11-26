#pragma once

#include <chrono>

#include "ch09/audit_record.hpp"
#include "ch09/handoff_queue_types.hpp"
#include "ch09/parcel.hpp"
#include "ch09/sync_audit_writer.hpp"

namespace ch09 {

using Clock = std::chrono::steady_clock;

AuditRecord build_audit_record(const Parcel& parcel);

// The "before": decide, then write, both on the hot path's own thread. The
// returned value is this one call's own duration (microseconds), the
// scan-to-divert hot-path cost this chapter measures lane against lane; it
// is this parcel's own processing time, not a running total since the
// trial's first parcel.
double process_parcel_sync(const Parcel& parcel, SyncAuditWriter& writer);

struct OffPathOutcome {
    double completion_us = 0.0;
    bool shed = false;
};

// The "after": decide, then hand the record to the off-path queue and
// return. push_with_backpressure's own bounded wait (see
// BoundedHandoffQueue) is the only way this can take longer than an
// in-memory enqueue; it never does the write itself.
template <typename QueueT>
OffPathOutcome process_parcel_offpath(const Parcel& parcel, QueueT& queue, std::chrono::microseconds max_wait) {
    const auto call_start = Clock::now();
    const AuditRecord record = build_audit_record(parcel);
    const bool queued = queue.push_with_backpressure(record, max_wait);
    const double duration_us = std::chrono::duration<double, std::micro>(Clock::now() - call_start).count();
    return OffPathOutcome{.completion_us = duration_us, .shed = !queued};
}

}  // namespace ch09
