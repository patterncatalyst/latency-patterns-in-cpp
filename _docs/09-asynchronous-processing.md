---
title: "Asynchronous processing"
order: 9
part: "Scaling and the Host"
description: "The scan-to-divert hot path's audit write, measured synchronous-inline against handed off to a background event-loop consumer (epoll multiplexing, request batching, a buffer pool, bounded backpressure), plus the identical hand-off in idiomatic Rust, where async/await and an executor replace the event loop, and off-path telemetry export to the LGTM stack."
duration: 35 minutes
---

## Learning objectives

By the end of this chapter you can:

- State the rule this chapter exists to teach: never instrument, log, or
  do other non-latency-critical work synchronously on the hot path; hand
  it off to a background consumer.
- Build a bounded, wait-free hot-path-to-consumer handoff, where the hot
  path's only job is to enqueue and return, never to wait.
- Read an event loop that multiplexes two readiness sources, a queue
  signal and a batch deadline, with `epoll`.
- Explain backpressure as a bound, not a policy: a full queue makes the
  producer wait up to a fixed limit, then sheds with accounting, never
  blocks without limit and never drops silently.
- Compare C++23's event-loop-plus-`epoll` design against Rust's
  async/await-plus-executor design for the identical hand-off, and name
  what changes when a runtime drives suspension instead of a blocked OS
  thread.

## Diagram

{% include excalidraw.html file="async-processing-offpath-handoff" alt="Two panels. Top: the hot path scans, decides the chute, and writes its audit record synchronously, O_DSYNC, blocking until durable. Bottom: the hot path scans, decides the chute, and enqueues the record into a bounded ring, returning immediately; a background event-loop consumer drains the ring in batches, flushes each batch with one writev or io_uring call, and ships telemetry off-path to an optional LGTM stack." caption="Figure 9.1: The hot path's own durable write, above, versus a non-blocking hand-off to a background consumer that batches the flush and exports telemetry off-path, below; the bounded ring sheds, with accounting, only once its backpressure wait is spent." %}

## Why this matters

Every chapter so far has made the scan-to-divert decision itself cheaper or
safer to parallelize. This chapter asks a different question: once the
decision is made, what happens to the work that decision triggers, the
label print, the audit-log entry, the telemetry, and does any of it still
have to happen before the next parcel can move?

This chapter follows one common failure: the audit log. Every diverted
parcel needs a durable record, parcel ID, destination chute, timestamp,
for compliance and for tracing a misroute later. The straightforward way
to build that record is to write it the moment the decision is made, on
the same thread that made the decision. That straightforward way is
exactly what inflates the tail, because a durable write is not fast, and
the hot path has no business waiting on one.

## The hot path that writes its own audit record

`examples/09-async-processing/cpp/`'s "before" lane opens its audit log
with `O_DSYNC`: every `write()` returns only once the record is durable,
not once it is buffered in the page cache. `ch09::SyncAuditWriter::write_record`
does exactly that, called directly from the function standing in for the
hot path:

```cpp
double process_parcel_sync(const Parcel& parcel, SyncAuditWriter& writer) {
    const auto call_start = Clock::now();
    const AuditRecord record = build_audit_record(parcel);
    writer.write_record(record);
    return std::chrono::duration<double, std::micro>(Clock::now() - call_start).count();
}
```

Nothing here is wrong in isolation: `O_DSYNC` is correct for a record that
must survive a crash, and `build_audit_record` is cheap. The problem is
where it runs, on the same thread, in the same call, as the decision the
scan-to-divert deadline is timed against. A flush to stable storage is a
real, variable-cost operation, measured below as 20,000 calls to this
function, the rate a real parcel line would call it.

## Moving the write off the hot path

The fix is not "write less often" or "write smaller records." It is "do
not write here." `ch09::BoundedHandoffQueue` is a fixed-capacity SPSC
ring, built the same wait-free way as "Wait-free synchronization"'s
`SpscRingQueue`: the hot path's `push_with_backpressure` touches nothing
but two atomics on its fast path. A background thread,
`ch09::EventLoopConsumer`, owns everything downstream: draining the ring,
batching what it drains, and flushing the batch durably. The hot path's
own function shrinks to this:

```cpp
OffPathOutcome process_parcel_offpath(const Parcel& parcel, QueueT& queue,
                                       std::chrono::microseconds max_wait) {
    const auto call_start = Clock::now();
    const AuditRecord record = build_audit_record(parcel);
    const bool queued = queue.push_with_backpressure(record, max_wait);
    const double duration_us =
        std::chrono::duration<double, std::micro>(Clock::now() - call_start).count();
    return OffPathOutcome{.completion_us = duration_us, .shed = !queued};
}
```

The write is gone from this function entirely; what is left is an
enqueue, two atomics and a 60-byte copy on the fast path, nothing that
touches a disk or a durability barrier.

### The event loop: two readiness sources, one `epoll_wait`

`EventLoopConsumer` runs on its own thread and blocks in `epoll_wait` on
two file descriptors: the queue's `eventfd`, signaled every time the hot
path enqueues a record, and a `timerfd`, armed for a fixed batch delay (5
milliseconds in this chapter's trials). Either one waking the loop up
means the same thing, drain what is available and consider flushing:

```cpp
const int ready = ::epoll_wait(epoll_fd_.get(), events.data(),
                                static_cast<int>(events.size()), -1);
```

The `eventfd` means "a batch might be full, go check." The `timerfd` means
"this batch has waited long enough, flush it anyway." Without the timer, a
half-full batch would sit indefinitely once arrivals taper off; the
deadline bounds how stale an unflushed record can get. This is the
portable baseline: `epoll` ships on every Linux kernel this book targets
and multiplexes an arbitrary number of readiness sources the same way.

### Batching, a buffer pool, and the faster path

The consumer writes a batch as one unit, not one call per record.
`ch09::BlockingBatchAuditWriter::flush_batch` turns a batch into one
`writev()` over an array of `iovec`s, so one `O_DSYNC` durability barrier
covers the whole batch. `ch09::BufferPool` supplies the batch buffer: a
small set of pre-sized `std::vector<AuditRecord>`s the consumer checks
out, fills, and returns, so steady-state operation never allocates.

Where `liburing` resolves (pinned exactly via Conan), `ch09::IoUringBatchAuditWriter`
replaces the `writev()` with one `io_uring_prep_writev` submission and a
single `io_uring_wait_cqe`, the same one-flush-per-batch contract via
`io_uring` instead of a blocking syscall. An earlier version submitted one
SQE per record; it was barely faster than sync-inline, because `O_DSYNC`
treats every individual write as its own durability point regardless of
submission batching. One `writev`-shaped SQE for the whole batch, not one
per record, is what buys the amortization. Where `liburing` cannot
resolve, the example still builds and runs on `epoll` plus blocking
`writev` alone; that fallback is the portable floor.

### Backpressure: a bound, not a policy

A hot path that enqueues faster than the consumer drains will eventually
fill the ring. `push_with_backpressure` handles that with a bounded wait,
never a choice between blocking forever and dropping silently:

```cpp
if (next == head_.load(std::memory_order_acquire)) {
    std::unique_lock lock(backpressure_mutex_);
    const bool made_room = backpressure_cv_.wait_for(
        lock, max_wait, [&] { return next != head_.load(std::memory_order_acquire); });
    if (!made_room) {
        shed_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}
```

If the consumer frees a slot before `max_wait` elapses, the push succeeds
and the caller never knows the ring was briefly full. If it does not, the
record is shed and `shed_count_` counts it. Nothing here drops a record
without accounting for it, and nothing blocks the hot path longer than
`max_wait`, no matter how far behind the consumer falls: a producer that
cannot be served in time degrades in a way the system can measure, not in
a way that cascades into an unbounded queue or an unresponsive hot path.

## The measurement

`examples/09-async-processing/cpp/` runs 20,000 parcels through the
sync-inline lane and the off-path lane, each parcel's hot-path call timed
on its own, not as a running total since the batch started. Both lanes
write to a real file on this host's SSD, not a RAM-backed filesystem,
since `O_DSYNC` on `tmpfs` does not exercise a real durability barrier and
would understate the effect this chapter measures. From
`results/async-processing.txt`, measured on a 16-core/22-thread laptop
CPU, powersave governor, bare metal, no pinning or isolation:

| Lane | p50 | p99 | p99.9 | throughput |
|---|---|---|---|---|
| sync-inline (one `O_DSYNC` write per parcel) | 442.029 us | 920.049 us | 1516.785 us | 2,136.8 items/s |
| off-path (queue capacity 4,095, batch 64, `max_wait` 5,000 us) | 0.970 us | 333.342 us | 461.725 us | 162,327.8 items/s |

The off-path median is effectively free, about 456 times smaller than
sync-inline's, because most parcels never touch anything slower than an
atomic store. The tail is not free: the ring's high-water mark reached its
full 4,095-slot capacity at least once, and a fraction of parcels paid a
bounded wait for the consumer to catch up, though even that wait's p99.9
(461.725 microseconds) sits under half of sync-inline's own p99 (920.049
microseconds). Throughput tells the same story: off-path clears about
76.0 times more parcels per second, batching turning 64 flushes into one.
`offpath_vs_sync_mismatch_count` reads 0: the two lanes' audit files hold
the identical set of parcel records, no duplicates, confirming the
hand-off changes nothing about what gets written, only when.

A second, smaller trial forces backpressure to actually shed: a 63-slot
queue and a 50-microsecond `max_wait` against the same unpaced push.
`backpressure_shed_count` reads 1,176 of 20,000 (5.9 percent), and
`backpressure_missing_from_audit_count` reads exactly 1,176 too, every
shed record accounted for and nothing else missing; delivered parcels
still show a bounded tail, p99.9 at 108.634 microseconds, the `max_wait`
bound holding even under a queue this undersized.

A secondary benchmark, 2,000 batches of 64 records, compares the
consumer's flush backend directly: blocking `writev` against `io_uring`.
Medians sit close together (443.431 versus 453.322 microseconds), both
already amortized by batching; `io_uring`'s p99.9 runs about 4.7 times
tighter (1,296.066 against `writev`'s 6,038.691 microseconds), real but
narrow, not the headline.

Hedging, duplicating a slow request and taking whichever answer returns
first, belongs in this chapter's margins: a camera stalled on a dirty lens
is the same shape of problem as a slow backend call, and firing a second
camera the instant the first looks late trades a little redundant work
for a bounded worst case, the trade this chapter's backpressure bound
makes in the other direction. Memory-mapping a label-print spool file
instead of calling `write()` is the same idea again, letting the page
cache absorb the latency; neither gets a measured lane here, since the
audit-write hand-off already demonstrates the pattern both share: move
the slow part off the path a deadline is timed against.

## The C++/Rust side-by-side: two async models, not two languages

This is the third and final chapter with a Rust lane, and it is not about
ownership the way "Wait-free synchronization" and "Exploiting concurrency"
were. It is about a different kind of model difference: C++23 expresses
the off-path consumer as an OS thread blocked in `epoll_wait`, an event
loop the programmer wires up file descriptor by file descriptor. Rust
expresses the identical consumer as an `async fn`, driven by an executor,
where "wait for either the queue or the timer" is a single expression:

{% capture cpp_wait %}
```cpp
std::array<epoll_event, 3> events{};
const int ready = ::epoll_wait(epoll_fd_.get(), events.data(),
                                static_cast<int>(events.size()), -1);
// ... inspect events[i].data.fd against queue_.event_fd(), timer_fd_, stop_fd_
```
{% endcapture %}
{% capture rust_wait %}
```rust
let next = smol::future::or(
    async { Some(receiver.recv().await.ok()) },
    async { Timer::after(remaining).await; None },
)
.await;
```
{% endcapture %}
{% include code-tabs.html id="offpath-wait-multiplex" cpp=cpp_wait rust=rust_wait %}

Both ask the same question, queue-ready or deadline-first, backed by the
same mechanism underneath (`smol`'s reactor polls with `epoll` too); what
differs is where that mechanism is visible. The C++ side hand-registers
file descriptors with `epoll_ctl`, reads raw `epoll_event`s, and branches
on which `fd` fired; a fourth readiness source means a fourth call and a
fourth branch. The Rust side composes futures with `or()`; a third source
is a third argument, not a new descriptor to track by hand. Rust trades
the explicit event loop for composition: suspension points (`.await`) are
visible, but descriptor bookkeeping disappears into the runtime.

Shutdown is where the two models diverge in substance, not just syntax. The C++
consumer needs a dedicated `stop_fd_` eventfd: a `std::stop_callback` on
the worker's `std::stop_token` writes to it so a blocked `epoll_wait`
wakes when the owning object is destroyed, mirroring "Exploiting
concurrency"'s rule that a thread-owning type declares its thread member
last. The Rust consumer needs no equivalent primitive: every `Sender`
dropping closes the channel, `receiver.recv().await` surfaces that as
`Err`, and the consumer flushes its last partial batch and returns.
Ownership is the stop signal, not a library choice.

`smol`, not `tokio`, is the dependency here: it bundles a minimal
`block_on` executor with an `epoll`-backed reactor in one small crate, the
comparison this chapter needs (event loop against executor), not "which
production runtime should a service pick," the question `tokio`'s
work-stealing scheduler answers instead. `libc` is the other direct
dependency, reaching `O_DSYNC` through `OpenOptionsExt::custom_flags`
since `std::fs` has no durable-write flag of its own; both pinned exactly.

The same trials against the Rust lane, release mode the way anyone
measuring Rust would (the C++ numbers come from this book's unoptimized
Debug preset, so the two differ in magnitude for that reason alone,
labelled as the same technique in Rust, not a comparison): sync p50 =
440.430 us, p99 = 950.558 us, p99.9 = 1428.810 us; off-path p50 = 0.238
us, p99 = 452.025 us, p99.9 = 555.459 us, `shed_count` = 0; the
backpressure demo sheds 2,203 of 20,000. The shape matches the C++ lane's
for the reason that matters: both pay the identical `O_DSYNC` cost
inline, and both make it nearly disappear from the median off-path.

## How the code works

**The handoff.** `ch09::BoundedHandoffQueue<T, Capacity>` is a
power-of-two ring with `head_`/`tail_` each on their own cache line, the
same layout "Wait-free synchronization" used. `push_with_backpressure`
adds the one thing that ring's ancestor left out: a bounded
`condition_variable` wait, entered only when the ring is actually full.

**The event loop.** `ch09::EventLoopConsumer<QueueT>` owns an `epoll`
instance and three file descriptors (the queue's `eventfd`, a `timerfd`,
and its own `stop_fd_`), each wrapped in a move-only `ScopedFd` that
closes on destruction. Its `std::jthread worker_` is declared last, so
the thread joins, running a final drain-and-flush loop, before the
descriptors it still touches are closed.

**The writers and trials.** `ch09::SyncAuditWriter` (the "before") and
`ch09::BlockingBatchAuditWriter` / `ch09::IoUringBatchAuditWriter` (the
"after") share one on-disk record layout, so `read_all_records` can
compare what each lane wrote. `ch09::run_sync_inline_trial` and
`ch09::run_offpath_trial<QueueT>` both return an `AsyncTrialResult`
carrying every parcel's own hot-path duration, a shed count, and a
high-water mark; `main.cpp` runs both plus the backpressure demo and the
flush-backend benchmark, cross-checks the record sets, and writes
`results/async-processing.txt`.

**The Rust lane.** `ch09_async_processing::process_parcel_sync` and
`process_parcel_offpath` (`rust/src/lib.rs`) are the synchronous hot-path
functions; `run_consumer` is the crate's one `async fn`.
`rust/src/bin/async_bench.rs` runs the same lanes and writes
`results/async-processing-rust.txt`.

**Off-path observability.** `ch09::TelemetrySink` is called only from
`EventLoopConsumer::drain_and_flush`, never the hot path, and is a no-op
unless `CH09_OTLP_ENDPOINT` is set, in which case it posts two OTLP/HTTP
JSON metrics (batch size, flush duration) to an OpenTelemetry Collector;
`observability/docker-compose.yml` stands up the house `lgtm-docker-stack`
destination, confirmed live. Nothing above depends on that stack being up;
`demo.sh` never starts it.

## Trade-offs

Moving work off the hot path does not make it free; it moves where the
cost is paid. Sync-inline pays in full, synchronously, on the exact thread
whose deadline it threatens. Off-path still pays the cost, on the
consumer's own schedule, batched, which is why it stops threatening the
deadline at all, but only because the queue between them is bounded and
the backpressure contract is explicit: an unbounded queue would hide the
exact failure mode this design exists to avoid, a consumer permanently
behind letting memory grow without limit instead of shedding with a
number attached.

Ordering and error handling move across that boundary too, and both get
harder to reason about once they do. A record dropped at the queue has no
stack trace back to the parcel that produced it; `shed_count` and
`missing_from_audit_count` exist because an accounted loss is the minimum
this design owes a reader explaining a gap later. A batch that fails to
flush fails on the consumer thread, not the hot path's call stack, so the
hot path has no way to learn about it synchronously; a real deployment
needs the consumer to surface that failure itself, because the hot path
cannot.

An `async` executor's advantage, many cheap tasks instead of many OS
threads, only pays off with enough concurrent tasks to amortize it; one
consumer, one queue, is not that case, and this chapter's Rust lane spends
a whole OS thread on `smol::block_on` for the same reason the C++ lane
does: there is exactly one job for it to do.

## What you learned

- The rule this chapter exists to teach: never instrument, log, or do
  other non-latency-critical work synchronously on the hot path; hand it
  off to a background consumer and let the hot path return immediately.
- A bounded, wait-free ring keeps the hand-off itself cheap; `epoll`
  multiplexing a queue-ready signal against a batch-deadline timer is what
  lets a single background thread batch without either starving or
  stalling.
- Backpressure is a bound, not a policy: a full queue costs the producer a
  capped wait, and only sheds, with accounting, once that bound is spent;
  nothing here blocks without limit or drops without a number attached.
- Batching amortizes a durable flush's real cost across many records;
  `io_uring` sharpens that further at the tail, but the batching itself is
  most of the win, and submitting one SQE per record instead of one per
  batch throws that win away even while still using `io_uring`.
- C++'s event loop and Rust's async/await plus executor solve the
  identical multiplexing problem in two different shapes: manual
  descriptor bookkeeping against composed futures, and a dedicated stop
  signal against ownership itself as the shutdown contract.

The next chapter leaves this host behind: taming the jitter a general-
purpose kernel scheduler imposes on a workload that wants real-time
guarantees, in a disposable virtual machine this book never applies to the
development host itself.

## For deeper coverage

- Pekka Enberg, *Latency: Reduce Delay in Software Systems* (Manning,
  2025): this guide's reference work for chapter arc and coverage; see
  `bibliography.md`.
- Jens Axboe, "Efficient IO with io_uring" (kernel.dk): the `io_uring`
  design document, from the engineer who built it into the Linux kernel.
- "Asynchronous Programming in Rust" (rust-lang.github.io/async-book): the
  executor and reactor model this chapter's Rust lane builds on, from the
  Rust project itself.

---

*Verification status: <span class="status status--verified">verified</span>.
C++ built with the default CMake/Conan lane; `ctest --preset default`
passes (176 tests total, including this chapter's new GoogleTests across
`BoundedHandoffQueue`, `BufferPool`, `SyncAuditWriter`, `BatchAuditWriter`,
`IoUringBatchAuditWriter`, `EventLoopConsumer`, the hot-path functions, and
the trial runners; every prior chapter's tests still pass unchanged). The
ASan/UBSan lane (`cmake --preset asan-ubsan`) builds clean and its own
`ctest --preset asan-ubsan` passes with no sanitizer report. Because this
chapter is concurrent by definition, the TSan lane (`cmake --preset tsan`)
was built and both `ctest --preset tsan` and the example binary itself
were run directly under it; the multithreaded `EventLoopConsumer` test
suite was additionally looped 20 times under TSan, zero `ThreadSanitizer`
reports across every run. `examples/09-async-processing/demo.sh run` ran
on an unprivileged, untuned development host and produced
`results/async-processing.txt` with `offpath_vs_sync_mismatch_count` and
every `duplicate_count` at `0`, the backpressure demo's `shed_count` and
`missing_from_audit_count` agreeing exactly, and the off-path lane beating
the sync-inline lane's throughput by about 76.0 times; every number above
was copied directly from that file. `python3 -I scripts/validate.py`
passes (host-ids, distribution, harness-cap).

Rust built against the pinned 1.100.0 toolchain. `cargo fmt --check` and
`cargo clippy --all-targets -- -D warnings` on
`examples/09-async-processing/rust/` are both clean. `cargo test` passes 6
unit tests (audit-record round-tripping, deterministic generation,
sync-inline correctness, off-path agreement with sync-inline on the exact
set of records, and backpressure shedding fully accounted for). `cargo
build --release` plus `async_bench` produced
`results/async-processing-rust.txt`, reported above as the same technique
in Rust, not a comparison against the C++ numbers.

The off-path OTLP export was confirmed live against
`examples/09-async-processing/observability/docker-compose.yml` (the house
`lgtm-docker-stack` destination): with the stack up and
`CH09_OTLP_ENDPOINT` set, `ch09_batch_size_ratio` and
`ch09_flush_duration_us_microseconds` both appear in the stack's
Prometheus label set. That stack was brought down again immediately after
and is not required by `demo.sh` or by either results file above.*
