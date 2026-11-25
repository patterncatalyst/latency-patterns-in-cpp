---
title: "Caching"
order: 7
part: "Scaling and the Host"
description: "A barcode-to-route cache sits in front of a slow backing resolver built to cost something real, measured across four replacement policies (LRU, LFU, FIFO, SIEVE) on the same Zipf-skewed access trace, plus TTL staleness and a memoized pure function."
duration: 35 minutes
---

## Learning objectives

By the end of this chapter you can:

- Explain why a cache only pays for itself on a skewed access pattern,
  and what a uniform-random trace would have hidden.
- Implement LRU, FIFO, and O(1) LFU from their textbook shapes, and
  implement SIEVE (a FIFO queue, one visited bit per entry, a moving
  hand) correctly from the technique itself.
- Read a hit-ratio number next to a latency distribution and say what
  each one tells you that the other does not.
- Use a TTL to bound how long a cache can serve an answer the backing
  source already changed, and say what that bound costs in hit ratio.
- Explain why memoizing a pure function needs no eviction policy, and
  why that makes it a different kind of cache from the other four.

## Diagram

{% include excalidraw.html file="caching-cache-and-sieve" alt="Two panels. Left: a cache sits in front of RouteResolver, a slow linear-scan backing lookup; a hit returns straight from the cache, a miss resolves, inserts, and returns. Right: a SIEVE queue of four entries, front to back; two middle entries are marked visited, the hand arrow points at the oldest (back) entry, unvisited, which is the one evicted." caption="Figure 7.1: A cache-aside lookup on the left; on the right, SIEVE's hand scans from the back toward the front, clearing a visited bit as a second chance and evicting the first entry it finds still unvisited." %}

## Why this matters

The previous chapter kept the same single box and asked what happens when
more than one thread shares the hot path. This chapter keeps the single
box and asks what happens when the same answer gets asked for more than
once.

A sortation line's scan stage does not see its barcodes evenly: a handful
of SKU families account for most of a shift's volume, and the rest trickle
in one at a time. Resolving a barcode to a chute by asking a backing table
every time pays the same cost whether the barcode is today's best-seller
or a part nobody has shipped in months. A cache in front of that table
changes the economics: keep the answer close for whatever is likely to be
asked again, and pay the slow path only for what is not.

The code is in `examples/07-caching/`. The run script there builds, runs,
and prints the results; its `README.md` covers what it does and how to
drive it.

## The slow backing lookup, and why the trace must be skewed

`ch07::RouteResolver` stands in for the real backing table or service a
cache would sit in front of: a disk-backed index, a routing microservice,
anything whose per-call cost is real and worth avoiding twice.
`resolve(barcode_key)` walks a 2048-entry table from the front, folding
each scanned entry's key into a running checksum along the way, a
stand-in for the per-row verification work a real backing scan pays
(confirming a row, decoding a sector, deserializing a response). The scan
is a chosen shape, worse than a real index would be; that gap is exactly
the cost a cache exists to hide.

Four eviction policies only disagree when there is something to disagree
about. On a uniform-random trace over the 2048-key space with a 128-entry
cache, every policy sees almost the same stream of cold, unrepeated keys
and evicts almost at random, so the four hit ratios land within noise of
each other. `ch07::ZipfTrace` instead draws from a Zipf distribution
(exponent 1.1), so a small hot set accounts for most of the accesses and a
long tail accounts for the rest, the shape a handful of fast-moving SKUs
produce against a catalog of thousands. The generator also shuffles which
barcode gets which popularity rank independently of where that barcode
sits in `RouteResolver`'s table, so a popular key is not quietly cheaper to
resolve for reasons that have nothing to do with the cache.

## Four ways to choose a victim

All four caches below share one shape: `get_or_insert(key, loader)`
returns the cached value on a hit, or calls `loader` and inserts the
result on a miss, evicting something if the cache is already full. What
differs is only which entry gets evicted.

**LRU** (`ch07::LruCache`) evicts whichever key has gone longest untouched:
a `std::list<int>` holds keys most-recently-used to least, and a hit
splices its entry to the front, an O(1) move.

**FIFO** (`ch07::FifoCache`), the simplest of the four, is a `std::deque`
of keys in insertion order; a hit changes nothing about that order. The
oldest *insertion* is evicted, not the oldest *access*, exactly the
distinction the measurement below isolates.

**LFU** (`ch07::LfuCache`) evicts whichever key has been touched fewest
times, via the standard constant-time structure (K. Shah, A. Mitra, and D.
Matani's O(1) LFU design; see "For deeper coverage"): one bucket per
frequency and a `min_freq_` counter tracking the lowest non-empty one. A
hit moves a key up one bucket; an eviction pops the key touched longest
ago from the back of the minimum bucket.

**SIEVE** (`ch07::SieveCache`, Zhang, Yang, Yue, Vigfusson, and Rashmi;
see "For deeper coverage") never reorders anything on a hit. Entries sit
in a `std::list`, newest at the front; a hit only sets one bit, `visited`.
Eviction walks a persistent "hand" from the back toward the front: a
visited entry's bit clears and the hand moves on (a second chance), and
the first entry still unvisited is the victim, with the hand left one step
ahead for next time. Built from that description, not ported from
reference code.

Each policy's GoogleTest drives a small, fully traced sequence and checks
the exact victim by name; SIEVE's evicts a never-touched new insertion
before an older entry touched once, telling it apart from plain FIFO.

## The measurement

All four lanes and the uncached baseline run the identical 50,000-access
Zipf trace against the identical `RouteResolver`. Measured on a
16-core/22-thread laptop CPU, powersave governor, bare metal, no pinning
or isolation, from `results/caching.txt`:

| Lane | Hit ratio | p50 | p99 | p99.9 |
|---|---|---|---|---|
| uncached (no cache at all) | 0.0% | 1.947 us | 4.185 us | 7.141 us |
| FIFO | 59.4% | 0.073 us | 4.648 us | 9.545 us |
| LRU | 64.8% | 0.090 us | 3.587 us | 5.401 us |
| LFU | 71.9% | 0.328 us | 3.837 us | 4.921 us |
| SIEVE | 72.7% | 0.055 us | 3.583 us | 5.365 us |

SIEVE wins the hit ratio outright (72.7% against LFU's 71.9%, a real if
narrow gap of 405 more hits out of 50,000) and also posts the lowest
median and p99. FIFO loses on every column: ignoring access means a hot
key evicted only because something newer arrived has to pay the full
backing-resolver cost to come back, the worst hit ratio and the worst
tail. LFU tells a different story at p50 than at p99.9: its per-hit
bucket move costs more than LRU's or SIEVE's O(1) splice, so its median is
slowest of the four, but that same frequency tracking gives it the
best-behaved tail. Hit ratio and latency measure different things, and a
policy can win one without the other.

Against the uncached baseline, SIEVE's median is about 35 times faster
(1.947 microseconds down to 0.055). At the tail the win shrinks to about
1.3 times (7.141 down to 5.365 at p99.9), because a miss still pays the
same backing-scan cost the uncached lane pays every time, and misses
dominate the tail. A cache buys its win at the median, where hits
dominate; it does not shrink the backing table.

`mismatch_count` reads `0` for all five lanes: every served chute matches
`RouteResolver::ground_truth` at the moment of that access.

## TTL and the staleness it buys back

A cached answer can go stale. If a parcel's SKU family gets reassigned to
a different chute (a re-route) while the old chute is still cached, the
cache has no way to know, and keeps serving the old answer until that
entry is evicted for an unrelated reason. A TTL bounds how long that can
go on: `ch07::TtlLruCache` is an LRU cache that also stamps each entry
with an insertion time and treats anything older than `ttl` as absent.

This chapter's trial reroutes the trace's ten hottest keys halfway through
a 50,000-access run, then measures two TTL windows against that reroute:

| TTL window | Hit ratio | Stale serves (post-reroute) |
|---|---|---|
| 2,000 accesses | 64.65% | 512 |
| 1,000,000 accesses (effectively never, for this trace) | 65.21% | 9,219 |

The long window wins hit ratio by half a percentage point and loses almost
eighteen times over on staleness: a window that outlives the trial just
keeps serving a rerouted key's old chute for the rest of the run. The
short window forces a refresh often enough to cut stale serves by 94% for
a hit-ratio cost under one percentage point. Neither number is "correct"
in the abstract; the right TTL is whatever staleness the business can
tolerate against whatever backing-resolver cost it will keep paying to
avoid it.

## Memoization: a cache that never evicts

`ch07::classify_dimensions` is a pure function: the same
(length, width, height) always produces the same `SizeClass`, derived from
a real, repeatable computation (an iterative cube root standing in for a
packing-radius derivation, folded with girth). `ch07::Memoizer` wraps it in
an `std::unordered_map` that never evicts anything, because nothing it
holds can ever go wrong: if `f(key)` cannot change for a given key, there
is nothing to invalidate. LRU, LFU, FIFO, and SIEVE exist because
`RouteResolver` *can* change underneath them; a memoized pure function
needs no such policy, only somewhere to keep an answer it never has to
revise.

Against a Zipf-skewed trace of 24 standard parcel dimension tuples
(50,000 calls), the direct path measures 0.293 microseconds at p50; the
memoized path measures 0.038, about 7.7 times faster, with a 99.95% hit
ratio (49,976 hits against exactly 24 misses, one per distinct tuple) and
zero mismatches against calling `classify_dimensions` directly.

A related idea, named here but not built: a **materialized
view** precomputes and stores an entire query result rather than one
function's answer, the same bet (pay once, read many times) applied to a
whole result set instead of one key at a time. Distributed caching and
cache coherency across more than one node are also out of scope:
everything here lives in one process on one box, and keeping two or more
caches consistent with each other over a network is a v2 topic.

## How the code works

**The access trace.** `ch07::ZipfTrace` precomputes a cumulative Zipf
distribution over `[0, key_space)` once; `generate(length)` then draws a
uniform random number per access and binary searches the cumulative table
for its rank. A second, independently seeded shuffle maps "popularity
rank" to "which key is that popular," so a hot key's position in
`RouteResolver`'s table, and its backing-scan cost, is unrelated to how
often the trace asks for it.

**The trial runner.** `ch07::run_cache_trial` and `ch07::run_uncached_trial`
do the same three things per access: time the call, record hit or miss,
and compare the served value against `RouteResolver::ground_truth`.
`ch07::run_ttl_trial` adds a synthetic clock (`epoch + access_index *
tick`) fed into `TtlLruCache::get_or_insert` instead of the real one, so a
TTL window means the same number of accesses on every host; the reroute
itself is one call to `RouteResolver::reroute` right before the access at
`reroute_at_index`. Every cache keys on `int`, not a template parameter,
since this chapter only ever caches one thing, a barcode resolving to a
chute, and keeping the key type concrete keeps the eviction logic free of
unrelated template noise.

## Build, run, observe

```bash
cd examples/07-caching && ./demo.sh
```

The run prints every lane's hit ratio and latency percentiles, plus
`mismatch_count` (0 for every eviction-policy lane) and
`stale_serve_count` (nonzero for both TTL lanes after the simulated
reroute, by design). Full distributions and histograms land in
`results/caching.txt`.

## Trade-offs

A cache only pays for itself when the access pattern is skewed enough for
hits to be common; on a uniform workload, every policy converges toward
the uncached number, because there is nothing left for a hit to save.
Cold start is the same problem from another angle: every cache here opens
at 0% hit ratio and pays the full miss cost for its first pass through the
working set, and a cache flushed or restarted often never gets past that
opening cost. A bigger cache helps only up to the point where it covers
the hot set; beyond that, capacity is memory not spent on something else.
And invalidation, this chapter's TTL in one word, is a cost every cache in
front of a mutable source pays somehow: a bound on staleness, or a way for
the source to actively announce a change, which is out of scope here.

## What you learned

- A replacement policy only has something to prove on a skewed access
  trace; on a uniform one, all four converge to indistinguishable hit
  ratios because there is no locality to exploit.
- SIEVE's whole mechanism is one bit per entry and a hand that remembers
  where it left off. It wins this chapter's trace on hit ratio and keeps a
  competitive latency profile without LRU's per-hit splice or LFU's
  per-hit bucket move.
- Hit ratio and latency measure different things: LFU's frequency
  tracking buys the best p99.9 of the four policies at the cost of the
  slowest median, because its bookkeeping is not free.
- A TTL trades hit ratio for a bound on staleness; this chapter's short
  window cut stale post-reroute serves by 94% for well under one
  percentage point of hit ratio.
- Memoizing a pure function needs no eviction policy at all, because
  nothing it holds can ever become wrong.

The next chapter keeps the single box and asks what happens when the work
itself, not just the answers to it, gets partitioned: the in-flight parcel
tracker split by barcode hash across one worker per core.

## For deeper coverage

- Pekka Enberg, *Latency: Reduce Delay in Software Systems* (Manning,
  2025): this guide's reference work for chapter arc and technique
  coverage; see `bibliography.md`.
- Yazhuo Zhang, Juncheng Yang, Yao Yue, Ymir Vigfusson, and K. V. Rashmi,
  "SIEVE is Simpler than LRU: an Efficient Turn-Key Eviction Algorithm for
  Web Caches" (USENIX NSDI, 2024): the source of the eviction algorithm
  `ch07::SieveCache` implements.
- K. V. Shah, A. Mitra, and D. Matani, "An O(1) algorithm for implementing
  the LFU cache eviction scheme" (2010): the constant-time frequency-
  bucket structure `ch07::LfuCache` builds from.
- Martin Kleppmann, *Designing Data-Intensive Applications* (O'Reilly,
  2017): caching, staleness, and materialized views in the broader context
  of a system with more than one node, the context this chapter's v2
  backlog (distributed caching and cache coherency) belongs to.

---

*Verification status: <span class="status status--verified">verified</span>.
C++ built with the default CMake/Conan lane; `ctest --preset default`
passes (119 tests total, including this chapter's 33 new GoogleTests
across `RouteResolver`, `ZipfTrace`, all four eviction policies,
`TtlLruCache`, `Memoizer`, and `classify_dimensions`; every prior
chapter's tests still pass unchanged). The ASan/UBSan lane (`cmake
--preset asan-ubsan`) builds clean, its own `ctest --preset asan-ubsan`
passes (119 tests), and the example binary was run directly under that
lane with no sanitizer report. This chapter's example is single-threaded,
so there is no TSan lane for it (see CLAUDE.md, "Per-chapter deliverables":
TSan applies only to the concurrency chapters).
`examples/07-caching/demo.sh` ran on an unprivileged, untuned development
host and produced `results/caching.txt` with every `mismatch_count` field
at `0`, SIEVE winning hit ratio over the other three policies, and both
TTL lanes showing a nonzero `stale_serve_count` after the simulated
reroute (512 for the short window, 9219 for the long one); every number
above was copied directly from that file. `python3 -I scripts/validate.py`
passes (host-ids, distribution, harness-cap). `examples/_lib` and the
shared line-simulator were not touched; this chapter does not route its
trial through `LineSimulator` (the same choice the two prior chapters made
for a technique better measured as a direct trial than as a full line
run), so nothing about any earlier chapter changed.*
