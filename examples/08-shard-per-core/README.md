# Chapter 8 example: shard-per-core partitioning

This example measures one in-flight parcel tracker, keyed by barcode, two
ways: a single `std::unordered_map` behind one `std::mutex`
(`ch08::SharedLockTracker`), where every core serializes through the same
lock no matter which barcode it saw, and shard-per-core
(`ch08::ShardedTrackerPool`), where each of N shards owns its own map and
its own `std::jthread` worker, routed by a direct hash of the barcode, with
no lock shared across shards.

A second pair of trials induces hot-partition skew: two barcodes are found
(`ch08::find_colliding_hot_keys`) that collide onto the identical shard
under direct hashing, then given 60 percent of a trace's traffic between
them. The resulting imbalance is measured, then one of the two colliding
keys is moved to an otherwise idle shard through a routing-table
indirection (`ch08::RoutingTable`), and the imbalance is measured again.

## Layout

```
cpp/
├── include/ch08/
│   ├── tracked_state.hpp        # Barcode, TrackedState
│   ├── shared_lock_tracker.hpp  # the baseline: one map, one mutex
│   ├── sharded_tracker_pool.hpp # shard-per-core: N owned maps, N jthread workers
│   ├── routing.hpp              # hash_route (direct), RoutingTable (indirection), find_colliding_hot_keys
│   ├── affinity.hpp             # best-effort sched_setaffinity for the pool's own workers
│   ├── event_generator.hpp      # uniform and skewed barcode traces
│   └── partition_trial.hpp      # the two trial runners + result shape
├── src/
│   └── ... (one .cpp per header, plus main.cpp)
└── tests/                       # GoogleTest: routing, both trackers, both trials, skew detection
results/
└── shard-per-core.txt           # every lane's throughput, latency distribution, and per-shard load
demo.sh                          # build -> run -> show the result
```

## How to drive it

```bash
cd examples/08-shard-per-core
./demo.sh            # build the default (no-sanitizer) lane, run, print the result
./demo.sh build       # just build
./demo.sh run         # just run (needs a prior build)
```

`demo.sh run` pipes `scripts/profile.sh`'s host-profile line into the
example binary, which runs every lane and copies the profile header plus
every lane's throughput, latency distribution, and per-shard load into
`results/shard-per-core.txt`. The binary wraps the results directory in
`measure::ResultsDirScrubGuard`, the same host-id scrub every prior
chapter's example uses; `python3 -I scripts/validate.py` is the gate that
fails a commit on a leak.

## Behavioral verification

- `sharded_vs_shared_lock_state_mismatch_count` reads `0`: for every
  distinct barcode the batch touched, the shared-lock tracker's
  `seen_count` and the sharded pool's `seen_count` for that barcode's
  routed shard agree exactly, after the identical event sequence ran
  through both.
- The GoogleTest suite checks that `hash_route` is deterministic and total
  (every key lands in range, every time), that `RoutingTable`'s default
  assignment reproduces `hash_route` exactly even at a finer bucket
  granularity, that `ShardedTrackerPool` never lets one shard's map see a
  key routed to a different shard, and that a skewed trace produces a
  shard whose processed count is more than double any other shard's (see
  `tests/partition_trial_test.cpp`'s `SkewedTraceOverloadsTheHotKeysShard`).
- `skew_before_hot_vs_max_other_ratio` is expected to be large (this run:
  over 30), and `skew_after_hot_vs_max_other_ratio` is expected to drop
  sharply once the mitigation runs, though not to 1 shard's worth of idle
  capacity: see "What to look for."

## What to look for

- `sharded_throughput_items_per_sec` is far higher than
  `shared_lock_throughput_items_per_sec` on the identical uniform-keyspace
  batch: every core in the shared-lock lane pays for the one mutex no
  matter which barcode it saw, while shard-per-core pays for nothing
  shared at all.
- `shared_lock_p50_us`/`p99_us`/`p999_us` move together with the shared-lock
  lane's throughput loss: the whole batch's clear time grows, not just its
  tail, because the lock serializes the batch itself, not just its unlucky
  stragglers.
- `skew_before_hot_shard_processed_count` is far larger than
  `skew_before_max_other_shard_processed_count`: two barcodes chosen to
  collide onto one shard, given 60 percent of the trace's traffic, leave
  that one shard's single worker with most of the batch while the other
  shards idle.
- `skew_before_hot_shard_p50_us`/`p99_us`/`p999_us` run slower than
  `skew_before_other_shards_p50_us`/`p99_us`/`p999_us`: the overloaded
  shard's queue backs up, so every observation routed there waits behind
  everything ahead of it.
- `skew_after_hot_shard_processed_count` drops to roughly half its
  before-mitigation value once one of the two colliding keys moves to
  `skew_target_shard_index`, and that lane's own latency improves to
  match. `skew_after_max_other_shard_processed_count` is now the moved
  key's new shard, carrying nearly as much load as the original hot shard
  did: rebalancing one of two colliding keys cuts the single worst shard's
  load roughly in half, it does not erase the skew, because the moved
  key is still exactly as hot wherever it lands.

## Verification status

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane; `ctest --preset default` passes
(every test across every chapter, including this chapter's new GoogleTests
for routing, both trackers, both trial runners, and skew detection). The
ASan/UBSan lane (`cmake --preset asan-ubsan`) builds clean, its own
`ctest --preset asan-ubsan` passes, and the example binary runs clean under
that lane (no sanitizer report). Because this chapter is concurrent by
definition, the TSan lane (`cmake --preset tsan`) was built and
`ctest --preset tsan` was run 20 times in a loop plus the example binary
itself run directly under TSan, with zero `ThreadSanitizer` reports across
every run. `./demo.sh` was run on an unprivileged, untuned development host
and produced `results/shard-per-core.txt` with
`sharded_vs_shared_lock_state_mismatch_count` at `0`, shard-per-core
beating the shared-lock lane's throughput by a wide margin, and the induced
skew lane showing a clearly overloaded shard that the routing-table
rebalance only partially relieves, which is the effect this chapter
claims. `python3 -I scripts/validate.py` passes (host-ids, distribution,
harness-cap). The exact numbers are specific to the host that produced
them (see the profile header in `results/shard-per-core.txt`) and are not
a cross-machine benchmark; a re-run on the same host will differ slightly
from the numbers quoted in the chapter text, since every lane measures
real wall-clock cost on a shared, untuned scheduler.*
