---
title: "Shard-per-core partitioning"
order: 8
part: "Scaling and the Host"
description: "An in-flight parcel tracker measured as one shared map behind one lock versus shard-per-core (direct hash routing, no lock on the hot path), then the sharded design again under induced hot-key skew before and after a routing-table rebalance."
duration: 30 minutes
---

## Learning objectives

By the end of this chapter you can:

- Explain why a shared lock serializes every core that touches it,
  regardless of which key any one core was working on.
- Partition state by key hash so each core owns its own slice and the hot
  path never takes a lock another core could be waiting on.
- Tell direct hash routing apart from a routing-table indirection, and say
  what the indirection buys that a hash function alone cannot.
- Read a per-shard load table and say whether an imbalance comes from the
  workload's key distribution or the partitioning scheme itself.
- Explain why rebalancing one overloaded key onto an idle shard relocates
  an overload rather than fixing it.

## Diagram

{% include excalidraw.html file="shard-per-core-and-skew" alt="Two panels. Left: four core boxes all send arrows through one mutex into one shared map. Right: four shard boxes, each with its own map box beneath it and a private arrow, no cross-arrows between shards; the third shard's map is drawn larger and amber, labeled two hot keys, overloaded." caption="Figure 8.1: One lock serializes every core on the left; shard-per-core removes the lock on the right, but a skewed key distribution can still leave one shard's single owner overloaded while the rest sit idle." %}

## Why this matters

"Caching" kept a single shared structure and asked how to avoid asking it
the same question twice. This chapter keeps a single box too, but asks a
different question: once more than one core needs to update the *same
kind* of state at once, how do you keep every core out of every other
core's way?

The in-flight parcel tracker is a good proxy for why this comes up: a
sortation line's control software has to know, continuously, which
barcodes are currently between the scanner and a diverter, and how many
times and when each last crossed the scan point. Every core doing scan or
divert work updates that state, frequently and cheaply, until enough cores
try to do it at once.

## The shared-lock baseline and why it contends

The obvious implementation is one `std::unordered_map<Barcode, TrackedState>`
behind one `std::mutex` (`ch08::SharedLockTracker`). `observe()` locks,
updates the one entry for that barcode, and unlocks:

```cpp
void SharedLockTracker::observe(Barcode key, double timestamp_us) {
    const std::scoped_lock lock(mutex_);
    TrackedState& state = entries_[key];
    ++state.seen_count;
    state.last_seen_us = timestamp_us;
}
```

Nothing about this is wrong: it is correct under any number of callers,
and the critical section is small. The cost is structural, not a bug. Two
cores updating two unrelated barcodes still take the same lock, because
the lock protects the map, not any one key in it; a barcode from aisle
three and one from aisle nine have nothing to do with each other, and
`SharedLockTracker` still makes them wait on one another. Scale the number
of cores up and the lock, not the work, becomes the ceiling.

## Shard-per-core and routing

The fix this chapter measures is horizontal partitioning: split the
barcode space into N independent slices, called shards, and give each
shard to exactly one core. A barcode's shard is decided by a hash of the
key, not by where it sits in memory or when it arrived, so the split
depends only on the key itself. (Vertical partitioning, by contrast,
splits a record's *fields* across stores instead of splitting the *keys*,
useful when some fields are read far more often than others; this
chapter's `TrackedState` is two small fields read and written together, so
this chapter has no use for it and does not build it.)

`ch08::hash_route(key, shard_count)` is direct hash routing: mix the key's
bits (`mix64`, splitmix64's finalizer) and take it modulo the shard count.
There is no table and nothing to rebalance; a key's shard is a pure
function of the key and the shard count. `ch08::ShardedTrackerPool` owns
one shard per `std::jthread` worker: a plain `std::unordered_map`, touched
only by that one worker (`worker_loop`'s per-item update in
`sharded_tracker_pool.cpp` takes no lock at all), fed by a small per-shard
queue. No shard's map is ever touched by any other shard's thread, so
there is no lock on the hot path once a worker is running.

Request routing is a separate decision from the hash itself. Direct hash
routing computes the shard fresh every time and has nowhere to put a
correction. `ch08::RoutingTable` is the alternative: a key maps to one of
`bucket_count` buckets by the same hash, and a small table maps each
bucket to a shard. With one bucket per shard the two are identical; the
difference shows up the moment a bucket's assignment needs to move, since
`assign(bucket, shard)` reassigns one bucket in O(1) with no rehash and no
effect on any other bucket. "Hot-partition skew" below is exactly the
scenario that needs this.

## The measurement

`examples/08-shard-per-core/` runs an identical 400,000-event,
50,000-distinct-barcode batch through `SharedLockTracker` and through
`ShardedTrackerPool` (22 shards, direct hash routing), on a 16-core/22-thread
laptop CPU, powersave governor, bare metal, no pinning or isolation. From
`results/shard-per-core.txt`:

| Lane | throughput | p50 | p99 | p99.9 |
|---|---|---|---|---|
| shared-lock (22 contending threads) | 1,952,102 items/s | 105059.337 us | 202598.307 us | 204651.035 us |
| shard-per-core (22 shards, direct hash) | 16,159,404 items/s | 21891.279 us | 24224.703 us | 24674.474 us |

Shard-per-core runs about 8.3 times the shared-lock lane's throughput, and
every percentile moves together, roughly 4.8 times faster at the median
and about 8.3 times faster at both the tail columns. That "together" is
the tell: the shared lock does not just slow down the unlucky stragglers,
it slows down the whole batch's clear time, because every single update,
lucky or not, has to wait its turn at the one mutex. `sharded_vs_shared_lock_state_mismatch_count`
reads `0`: for every one of the 50,000 distinct barcodes the batch
touched, the two trackers, run independently over the identical event
sequence, agree exactly on how many times they saw it.

One implementation note belongs here, because it changed the result: the
first version of `ShardedTrackerPool` queued one item per observation, a
lock, a push, and a condition-variable wake per barcode, and that version
was *slower* than the shared-lock baseline, since the per-item handoff
cost more than the mutex it replaced. The fix, `submit_batch()`, hands
each shard its whole routed slice as one handoff, the same "pay once, not
per item" idea this book has used since "Eliminating work." Removing a
lock does not help if what replaces it pays its own synchronization cost
on every update.

## Hot-partition skew

Direct hash routing assumes traffic is spread evenly across the key space.
Real barcode traffic is not: a handful of SKUs move far more volume than
the rest of the catalog on any given shift, the same skew "Caching"
measured as a Zipf distribution. Under shard-per-core, a hot key does not
just cost more to look up, the way it would in a cache; it pins its entire
load onto exactly one shard's one worker.

`ch08::find_colliding_hot_keys` searches for two barcodes that
`hash_route` sends to the identical shard, and this chapter's skew trial
gives those two barcodes 60 percent of a 200,000-event trace between them,
the rest spread uniformly over an unrelated 20,000-key cold range.
Measured on the same host:

| Metric | Before mitigation |
|---|---|
| hot shard's share of the batch | 123365 / 200000 (61.68%) |
| busiest other shard | 3841 |
| hot-shard-to-busiest-other ratio | 32.12x |
| hot shard's p50 / p99 / p99.9 | 13319.483 us / 15815.677 us / 15860.200 us |
| every other shard's p50 / p99 / p99.9 | 9111.968 us / 10444.661 us / 10547.163 us |

One shard, out of twenty-two, carries nearly two-thirds of the batch, over
32 times the busiest of the other twenty-one, and runs roughly 1.5 times
slower at every percentile, because its single worker's queue backs up
behind a load no other worker carries. The other twenty-one shards are
nearly idle, which is the real cost: shard-per-core traded lock contention
for the risk of one worker becoming the bottleneck by itself.

## The mitigation, and what it actually fixes

The routing-table indirection from "Shard-per-core and routing" is the
lever: move one of the two colliding keys off the shard it shares with the
other, onto whatever shard is not already hot. `ch08::RoutingTable` needs
more buckets than shards for this to isolate one key without disturbing
the other; at one bucket per shard, a bucket's assignment *is* a shard's
assignment, and two keys that collide on a shard necessarily collide on
that single bucket too. At 1,024 buckets per shard, the default table
still reproduces direct hash routing exactly (`bucket_count` is a multiple
of `shard_count`, so bucket-modulo-shard-count is hash-modulo-shard-count),
while the two colliding keys land in different buckets with near
certainty, so only one of them has to move:

| Metric | After mitigation |
|---|---|
| hot shard's share of the batch | 63513 / 200000 (31.76%) |
| moved key's new shard | 63515 / 200000 (31.76%) |
| hot shard's p50 / p99 / p99.9 | 10403.047 us / 11756.259 us / 11781.066 us |

The hot shard's load drops by roughly half, and its own latency
distribution improves with it, about 22 percent faster at p50 and about 26
percent faster at both tail columns. The rest of the picture is not a
win: the shard the moved key
landed on now carries almost exactly as much load as the original hot
shard still does, 63515 against 63513. The skew did not shrink, it split
across two shards instead of concentrating on one. Both are still carrying
roughly seven times an even 1-in-22 share, and sixteen times what a merely
cold shard carries. Rebalancing a key that is hot all by itself only
relocates the hot spot; it does not cool it down. What would actually fix
a single pathologically hot key is splitting that key's own traffic across
several shards and summing the parts back together on read, a real
technique this chapter names rather than builds, since the tracker's
single-owner-per-key invariant would have to change to support it.

This chapter's whole measurement runs on one box; partitioning data and
rebalancing load across more than one *machine*, where a rebalance means
moving bytes over a network, is a later-version topic, and nothing here
builds a cluster.

## How the code works

**The tracked state.** `ch08::Barcode` and `ch08::TrackedState`
(`tracked_state.hpp`) are the shape every lane reads and writes: a count
and a last-seen timestamp, nothing more. `ch08::SharedLockTracker`
(`shared_lock_tracker.hpp/.cpp`) is the baseline, one map behind one
`std::mutex`, as shown above.

**Routing.** `ch08::hash_route` and `ch08::RoutingTable`
(`routing.hpp/.cpp`) are direct hashing and the indirection respectively;
`ch08::find_colliding_hot_keys` searches for a real collision under
whatever `hash_route` does for a given shard count, since that count comes
from `std::thread::hardware_concurrency()` at run time and differs by
host.

**Shard-per-core.** `ch08::ShardedTrackerPool`
(`sharded_tracker_pool.hpp/.cpp`) owns one `std::unordered_map`, one
queue, one mutex, and one condition variable per shard, plus one
`std::jthread` worker per shard. Its `workers_` member is declared last,
the same rule `examples/06-exploiting-concurrency/cpp/include/ch06/thread_pool.hpp`
applies to a single shared queue: declaration order is destruction order
reversed, so every worker joins, and stops touching its shard's state,
before that state is destroyed. Getting this order wrong is a data race
and a heap-use-after-free that only shows up under ThreadSanitizer, not in
a plain debug build.

**The trials.** `ch08::run_shared_lock_trial` partitions the batch into
contiguous slices across worker threads that all call the same tracker's
`observe()`; `ch08::run_sharded_trial` groups the batch by routed shard
and hands each shard its whole slice through one `submit_batch()` call.
`main.cpp` runs both pairs of lanes, cross-checks `seen_count` per key, and
writes `results/shard-per-core.txt`. Each shard's worker may best-effort
pin itself to one logical CPU (`ch08::pin_current_thread_best_effort`,
ordinary unprivileged `sched_setaffinity` on its own thread, never the
host); a sandboxed host that refuses the call still runs correctly.

## Trade-offs

Shard-per-core is not free, and not always the right call. A query that
needs every shard's answer, "how many barcodes are in flight right now,"
costs one lock and one map read under the shared-lock baseline, and a pass
over every shard under shard-per-core, with no single consistent snapshot
unless every shard pauses for it: `SharedLockTracker::entry_count()` is
O(1), summing `ShardedTrackerPool::shard_size()` across twenty-two shards
is not, and that gap only widens as shard count grows. A workload
dominated by cross-key queries, not per-key updates, should think hard
before sharding at all.

Skew is the other cost this chapter measured directly, not a theoretical
caveat: a hash function has no notion of which keys are hot, so it can
hand one worker a crushing share of the traffic while the rest wait for
work. A routing-table indirection lets that be corrected after the fact,
but only up to what the correction can actually do: moving a key changes
which shard is hot, not whether some shard is hot, unless the fix
addresses the key's own traffic, not just its address.

## What you learned

- A shared lock serializes every core that touches it, regardless of key:
  it protects the structure, not any one entry in it.
- Shard-per-core removes that serialization by giving each shard exactly
  one owner, at the cost of a single, consistent, O(1) view across shards.
- Direct hash routing needs nothing but the key and the shard count; a
  routing-table indirection costs a lookup but buys the ability to move
  one key's traffic without touching any other key's.
- A naive per-item queue handoff can cost more than the lock it replaces;
  batching the handoff, not just removing the lock, is what made this
  chapter's sharded lane roughly 8.3 times faster, not slower.
- Rebalancing one of two colliding hot keys cuts the single worst shard's
  load roughly in half here, a real measured win, but it relocates an
  overload rather than curing it; a key hot by itself needs its own
  traffic split across shards, not just moved between them.

The next chapter keeps the same single box and moves the question from the
hot path's state to the hot path's instrumentation: watching a running
system without the watching itself becoming the next bottleneck.

## For deeper coverage

- Pekka Enberg, *Latency: Reduce Delay in Software Systems* (Manning,
  2025): this guide's reference work for chapter arc and technique
  coverage; see `bibliography.md`.
- Martin Kleppmann, *Designing Data-Intensive Applications* (O'Reilly,
  2017): horizontal and vertical partitioning, request routing, and
  rebalancing across more than one node, the context this chapter's
  cross-node backlog belongs to.
- Maurice Herlihy, Nir Shavit, Victor Luchangco, and Michael Spear, *The
  Art of Multiprocessor Programming*, 2nd ed. (Morgan Kaufmann): the
  formal treatment of partitioned, lock-free designs this chapter's
  tracker is one concrete instance of.

---

*Verification status: <span class="status status--verified">verified</span>.
C++ built with the default CMake/Conan lane; `ctest --preset default`
passes, including this chapter's 29 new GoogleTests across routing, both
trackers, both trial runners, and skew detection, with every prior
chapter's tests still green. The ASan/UBSan lane builds clean, its own
`ctest --preset asan-ubsan` passes, and the example binary runs clean
under it. Because this chapter is concurrent by definition, the TSan lane
was also built: `ctest --preset tsan` passes, this chapter's test binary
was additionally run 20 times in a direct loop outside ctest, and the
example binary itself ran directly under TSan, with zero
`ThreadSanitizer` reports across all of it.
`examples/08-shard-per-core/demo.sh` ran on an unprivileged, untuned
development host and produced `results/shard-per-core.txt` with
`sharded_vs_shared_lock_state_mismatch_count` at `0`; every number above
was copied directly from that file. `python3 -I scripts/validate.py`
passes (host-ids, distribution, harness-cap). `examples/_lib` and the
shared line-simulator were not touched; like "Exploiting concurrency" and
"Caching," this chapter measures a direct trial rather than a full
`LineSimulator` run, so nothing about any earlier chapter changed.*
