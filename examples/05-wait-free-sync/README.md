# Chapter 5 example: wait-free synchronization

One handoff, measured three ways: a mutex-guarded queue in C++ (the
"before"), a wait-free SPSC ring in C++ (the "after"), and the identical
wait-free ring in Rust (the same technique, a different compile-time
guarantee). All three move `Decision` values (a parcel ID, a chute, and
the moment the decision was made) from one producer thread standing in
for the scan stage to one consumer thread standing in for the divert
stage.

**C++ mutex queue.** `ch05::MutexHandoffQueue` is a `std::queue` guarded by
one `std::mutex` and a `std::condition_variable`. `push()` always succeeds
(the queue is unbounded); `pop()` blocks until something is available or
the queue is closed.

**C++ wait-free ring.** `ch05::SpscRingQueue<T, Capacity>` is a
fixed-capacity ring with `head_`/`tail_` as `std::atomic<std::size_t>`,
each `alignas(std::hardware_destructive_interference_size)` to keep them
off the same cache line (see "Zero-copy data layout"). `try_push()` and
`try_pop()` never block; each returns immediately, full or empty, with the
index ordering (`memory_order_acquire`/`memory_order_release`) doing the
cross-thread handoff.

**Rust wait-free ring.** `ch05_wait_free_sync::channel()` builds the
identical ring (same capacity convention, same acquire/release pairing).
Its `Sender<N>`/`Receiver<N>` are `Send` but not `Clone` and not `Sync`:
there is exactly one producer handle and one consumer handle per channel,
and neither can be shared behind a reference, so the C++ ring's "nothing
stops two threads calling `try_push`" failure mode does not compile here.
See `rust/src/lib.rs`'s doc comment on `Sender` for the `compile_fail` test
that checks this on every `cargo test`.

Both C++ lanes run `handoff_item_count` (200,000) items with both threads
flat out, no pacing; the Rust lane runs the same count through its own
ring. None of the three are a cross-language benchmark against each other;
the Rust number is labelled "the same technique in Rust" everywhere it
appears, never compared against the C++ numbers.

## Layout

```
cpp/
├── include/ch05/
│   ├── decision.hpp           # the value handed producer to consumer
│   ├── mutex_handoff_queue.hpp
│   ├── spsc_ring_queue.hpp    # header-only: try_push/try_pop, acquire/release
│   └── handoff_trial.hpp      # HandoffTrialResult, run_*_handoff_trial
├── src/
│   ├── mutex_handoff_queue.cpp
│   ├── handoff_trial.cpp
│   └── main.cpp                # runs both C++ lanes, writes results/wait-free-sync.txt
└── tests/                       # GoogleTest: FIFO, no loss/duplication, both lanes
rust/
├── Cargo.toml
├── src/
│   ├── lib.rs                  # channel(), Sender, Receiver, unit tests, compile_fail doctest
│   └── bin/handoff_bench.rs    # runs the Rust lane, prints a results block to stdout
results/
├── wait-free-sync.txt           # both C++ lanes' distributions
└── wait-free-sync-rust.txt      # the Rust lane's distribution
demo.sh                          # build -> run -> show the C++ result
```

## How to drive it

### C++

```bash
cd examples/05-wait-free-sync
./demo.sh            # build the default (no-sanitizer) lane, run, print the result
./demo.sh build       # just build
./demo.sh run         # just run (needs a prior build)
```

`demo.sh run` pipes `scripts/profile.sh`'s host-profile line into the
example binary, which runs the mutex lane, then the ring lane, and copies
the profile header plus both distributions into
`results/wait-free-sync.txt`. The binary wraps the results directory in
`measure::ResultsDirScrubGuard`, the same host-id scrub every prior
chapter's example uses; `python3 -I scripts/validate.py` is the gate that
fails a commit on a leak.

To check the TSan lane directly against the example binary (not just the
GoogleTest suite, which already runs under `ctest --preset tsan`):

```bash
eval "$(scripts/toolchain-env.sh)"
conan install . -pr:a conan/profiles/clang23 --build=missing -of build-tsan -s build_type=Debug
cmake --preset tsan && cmake --build build-tsan
scripts/profile.sh ch05-tsan-smoke | \
  build-tsan/examples/05-wait-free-sync/cpp/ch05_wait_free_sync /tmp/ch05-tsan-smoke scripts/scrub-host-ids.py
```

No `WARNING: ThreadSanitizer` output means clean.

### Rust

```bash
scripts/bootstrap-rust.sh          # once, if .toolchain/rust/ doesn't exist yet
eval "$(scripts/rust-env.sh)"
cargo fmt --manifest-path examples/05-wait-free-sync/rust/Cargo.toml --check
cargo clippy --manifest-path examples/05-wait-free-sync/rust/Cargo.toml --all-targets -- -D warnings
cargo test --manifest-path examples/05-wait-free-sync/rust/Cargo.toml
cargo build --manifest-path examples/05-wait-free-sync/rust/Cargo.toml --release
scripts/profile.sh ch05-wait-free-sync-rust | \
  ./examples/05-wait-free-sync/rust/target/release/handoff_bench | \
  python3 -I scripts/scrub-host-ids.py --filter > examples/05-wait-free-sync/results/wait-free-sync-rust.txt
```

`cargo test` runs the unit tests in `rust/src/lib.rs` (FIFO order, no
loss/duplication, a full-ring boundary check, and a real two-thread
concurrent-correctness test) plus the doc test on `Sender`, which is
marked `compile_fail` and is expected to fail to compile; `cargo test`
reports it `ok` precisely because it does not compile.

## Behavioral verification

Every results field prefixed `*_dropped_count`, `*_out_of_order_count`, or
`*_duplicate_count` (C++) and `*_out_of_order_count`/`*_duplicate_count`
(Rust) should read `0`: both queue implementations, in both languages,
must deliver every item sent exactly once, in the order it was sent. The
GoogleTest suite checks this directly (`MutexHandoffQueue`,
`SpscRingQueue`, and `HandoffTrial` test fixtures, including a real
two-thread concurrent run for each queue type) and so does the Rust lane's
`#[test]` suite. `handoff_ring_full_retries` (C++) and seeing
`handoff_rust_ring_items_received` equal `handoff_rust_ring_items_sent`
(Rust) confirm the ring never silently dropped an item even when it filled
and the producer had to retry.

## What to look for

- `handoff_mutex_p50_us`, `_p99_us`, and `_p999_us` sit far above the
  matching `handoff_ring_*` fields: the mutex-guarded queue's consumer
  sleeps on a `condition_variable` between items, and waking it costs a
  futex round trip through the kernel scheduler, every single item. Under
  two threads running flat out, that cost compounds into queueing delay
  that grows across the run.
- `handoff_ring_p999_us` stays within a small multiple of
  `handoff_ring_p50_us`: the wait-free ring's cost per item is the same
  pair of atomic operations whether the ring is busy or not, so its tail
  stays close to its median.
- `handoff_mutex_dropped_count`, `handoff_ring_dropped_count`, and both
  languages' `*_out_of_order_count`/`*_duplicate_count` fields all read
  `0`: the wait-free ring is a performance fix, not a correctness
  trade-off.

## Verification status

*Verification status: <span class="status status--verified">verified</span>.
C++ built with the default CMake/Conan lane; `ctest --preset default`
passes (74 tests total, including this chapter's new `MutexHandoffQueue`,
`SpscRingQueue`, and `HandoffTrial` GoogleTests; every prior chapter's
tests still pass unchanged). The ASan/UBSan lane (`cmake --preset
asan-ubsan`) builds clean, its own `ctest --preset asan-ubsan` passes (74
tests), and the example binary runs clean under that lane. Because this
chapter is concurrent by definition, the TSan lane (`cmake --preset tsan`)
was built and both `ctest --preset tsan` (74 tests) and the example binary
itself were run directly under it, with no `ThreadSanitizer` report either
time. `./demo.sh` ran on an unprivileged, untuned development host and
produced `results/wait-free-sync.txt` with the ring lane faster than the
mutex lane at every percentile and every `*_dropped_count`/
`*_out_of_order_count`/`*_duplicate_count` field at `0`.
`python3 -I scripts/validate.py` passes (host-ids, distribution,
harness-cap).

Rust built against the pinned 1.100.0 toolchain
(`scripts/bootstrap-rust.sh`, `scripts/rust-env.sh`). `cargo fmt --check`
and `cargo clippy --all-targets -- -D warnings` are both clean; `cargo
test` passes 5 unit tests plus the `compile_fail` doc test on `Sender`,
confirmed to fail compilation (verified by relaxing
the guard and watching the same doc test fail with "compiled successfully,
but it's marked `compile_fail`", then reverting). `cargo build --release`
plus `handoff_bench` produced `results/wait-free-sync-rust.txt` with
`handoff_rust_ring_out_of_order_count=0` and
`handoff_rust_ring_duplicate_count=0`. The exact numbers in both results
files are specific to the host that produced them (see each file's profile
header) and are not a cross-machine or cross-language benchmark; a re-run
on the same host will differ slightly, and the mutex lane's absolute
numbers in particular vary with how the scheduler interleaves the two
threads.*
