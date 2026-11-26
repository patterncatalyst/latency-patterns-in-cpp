#pragma once

#include "ch09/audit_record.hpp"
#include "ch09/bounded_handoff_queue.hpp"

namespace ch09 {

// The headline off-path lane's queue: sized generously enough that the
// background consumer's own batching keeps it from filling under this
// chapter's steady-state trial.
using HandoffQueue = BoundedHandoffQueue<AuditRecord, 4096>;

// The backpressure trial's queue: deliberately small, so a producer running
// flat out (no pacing) fills it well before the consumer's batched flushes
// can drain it, forcing push_with_backpressure's bounded-wait-then-shed path
// to actually run.
using SmallHandoffQueue = BoundedHandoffQueue<AuditRecord, 64>;

}  // namespace ch09
