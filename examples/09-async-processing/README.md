# Chapter 9 example: asynchronous processing

This example measures the scan-to-divert hot path's audit-record write two
ways: `ch09::SyncAuditWriter`, a synchronous, durable (`O_DSYNC`) write on
the hot path's own thread, and the off-path hand-off, where the hot path
enqueues into `ch09::BoundedHandoffQueue` and returns immediately, while a
background `ch09::EventLoopConsumer` (`epoll` over an eventfd and a
timerfd) drains, batches, and durably flushes the records on its own
thread. A dedicated small-queue trial forces real backpressure shedding,
and a secondary benchmark compares the consumer's own batched-flush
backend, blocking `writev` against `io_uring` (if `liburing` resolved).
The identical off-path hand-off is reimplemented in idiomatic Rust under
`rust/`, built on async/await and the `smol` executor instead of an OS
thread blocked in `epoll_wait`.

## Layout

```
cpp/
├── include/ch09/
│   ├── audit_record.hpp          # the fixed-size record this chapter moves off the hot path
│   ├── parcel.hpp                # Parcel, generate_parcels
│   ├── bounded_handoff_queue.hpp # the wait-free SPSC ring + bounded backpressure
│   ├── handoff_queue_types.hpp   # HandoffQueue (headline) / SmallHandoffQueue (backpressure demo)
│   ├── buffer_pool.hpp           # reusable batch buffers
│   ├── sync_audit_writer.hpp     # the "before": one O_DSYNC write per record
│   ├── batch_audit_writer.hpp    # the "after": one flush per batch (blocking writev)
│   ├── io_uring_batch_writer.hpp # the faster path: one flush per batch via io_uring
│   ├── event_loop_consumer.hpp   # the background consumer: epoll(eventfd, timerfd)
│   ├── telemetry_sink.hpp        # optional off-path OTLP/HTTP metrics export
│   ├── hot_path.hpp              # process_parcel_sync, process_parcel_offpath
│   └── async_trial.hpp           # the two trial runners + result shape
├── src/      # one .cpp per header, plus main.cpp
└── tests/    # GoogleTest: every header above, plus end-to-end record-set agreement
rust/
├── Cargo.toml          # smol (=2.0.2) + libc (=0.2.191), both pinned exactly
├── src/lib.rs           # the same hand-off, async/await + smol
└── src/bin/async_bench.rs
observability/
├── docker-compose.yml   # the optional LGTM-stack destination for off-path telemetry
└── README.md
results/
├── async-processing.txt       # C++ lanes
└── async-processing-rust.txt  # Rust lane, same technique, not a comparison
demo.sh      # build -> run -> show the result (C++); `rust` subcommand for the Rust bench
```

## How to drive it

```bash
cd examples/09-async-processing
./demo.sh            # build the default (no-sanitizer) C++ lane, run, print the result
./demo.sh build       # just build
./demo.sh run         # just run (needs a prior build)
./demo.sh rust        # build (release) + run the Rust bench
```

`demo.sh run` pipes `scripts/profile.sh`'s host-profile line into the
example binary, which runs every lane and writes
`results/async-processing.txt`, wrapped in `measure::ResultsDirScrubGuard`,
the same host-id scrub every prior chapter's example uses.
`demo.sh rust` does the same for the Rust bench via `cargo build --release`
and `scripts/scrub-host-ids.py`. Both commands are entirely host-only: no
`sudo`, no docker, no network.

### Optional: off-path telemetry to the LGTM stack

```bash
docker compose -f observability/docker-compose.yml up -d
export CH09_OTLP_ENDPOINT=127.0.0.1:4318
./demo.sh run
docker compose -f observability/docker-compose.yml down
```

See `observability/README.md` for what to look for in Grafana. This is
documentation only: `demo.sh` never starts the stack, and
`CH09_OTLP_ENDPOINT` unset (the default) makes `TelemetrySink` a no-op, so
the measured numbers above never depend on it.

## Behavioral verification

- `sync_vs_input_mismatch_count` and `offpath_vs_sync_mismatch_count` both
  read `0`: the sync-inline lane's audit file and the off-path lane's
  audit file hold the exact same set of parcel records, with no
  duplicates (`*_duplicate_count` also `0`). The hand-off changes nothing
  about what gets written, only when and how.
- In the backpressure demo, `backpressure_missing_from_audit_count` equals
  `backpressure_shed_count` exactly: every parcel absent from the audit
  file is one of the accounted-for shed parcels, never a silent drop
  somewhere else.
- The GoogleTest suite checks the ring's FIFO order and bounded-wait
  shedding, that the buffer pool reuses buffers under steady-state
  acquire/release, that `BlockingBatchAuditWriter` and
  `IoUringBatchAuditWriter` both write every record in a batch correctly,
  and that `EventLoopConsumer` flushes every pushed record exactly once,
  including a final partial batch on shutdown (see
  `tests/event_loop_consumer_test.cpp`'s
  `EveryPushedRecordIsFlushedExactlyOnce`).
- The Rust crate's tests check the identical properties:
  `offpath_agrees_with_sync_on_the_set_of_records` and
  `backpressure_shedding_is_accounted_and_nothing_else_goes_missing`.

## What to look for

- `offpath_p50_us` sits far below `sync_p50_us` (effectively free versus
  one full durability flush per parcel), while `offpath_throughput_items_per_sec`
  runs tens of times higher than `sync_throughput_items_per_sec`, because
  batching turns many flushes into one.
- `offpath_high_water_mark` reaching the queue's full capacity confirms
  the ring actually filled during the run; `offpath_shed_count` staying
  at `0` with a generous `max_wait` shows the backpressure bound absorbing
  that pressure without ever needing to shed.
- The dedicated backpressure trial's small queue and short `max_wait` are
  small enough to push `backpressure_shed_count` well above zero, with
  `backpressure_missing_from_audit_count` matching it exactly.
- `flush_io_uring_p999_us` running tighter than `flush_blocking_p999_us`
  shows `io_uring`'s real but narrow tail advantage over blocking
  `writev`, once both are submitting one request per whole batch rather
  than one per record.

## Verification status

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane; `ctest --preset default` passes
(every test across every chapter, including this chapter's new
GoogleTests). The ASan/UBSan lane (`cmake --preset asan-ubsan`) builds
clean and its own `ctest --preset asan-ubsan` passes with no sanitizer
report. Because this chapter is concurrent by definition, the TSan lane
(`cmake --preset tsan`) was built, `ctest --preset tsan` and the example
binary itself were run directly under it, and the multithreaded
`EventLoopConsumer` test suite was additionally looped 20 times under
TSan, zero `ThreadSanitizer` reports across every run. `./demo.sh` ran on
an unprivileged, untuned development host and produced
`results/async-processing.txt` with every mismatch and duplicate count at
`0` and the backpressure demo's shed and missing counts agreeing exactly.
`python3 -I scripts/validate.py` passes (host-ids, distribution,
harness-cap). The exact numbers are specific to the host that produced
them (see the profile header in `results/async-processing.txt`) and are
not a cross-machine benchmark.

Rust built against the pinned 1.100.0 toolchain. `cargo fmt --check` and
`cargo clippy --all-targets -- -D warnings` on
`examples/09-async-processing/rust/` are both clean; `cargo test` passes 6
unit tests. `cargo build --release` plus `async_bench` produced
`results/async-processing-rust.txt`, reported in the chapter as the same
technique in Rust, not a comparison against the C++ numbers.

The off-path OTLP export was confirmed live against
`observability/docker-compose.yml`: with the stack up and
`CH09_OTLP_ENDPOINT` set, both metrics this chapter's `TelemetrySink`
emits show up in the stack's Prometheus label set. That stack is not
required by either results file above.*
