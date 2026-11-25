# Chapter 7 example: caching

This example measures one barcode-to-route cache in front of one slow
backing resolver built to cost something real (`ch07::RouteResolver`, a
linear scan over a 2048-entry table), across a single shared Zipf-skewed
access trace
(a small hot set, a long tail, the shape real SKU traffic has). Four
replacement policies run the identical 50,000-access trace: LRU, FIFO, LFU
(the standard O(1) frequency-bucket design), and SIEVE (a FIFO queue, one
visited bit per entry, a moving hand). A fifth trial runs the uncached
baseline, every access paying the full backing-resolver cost, for
comparison.

A second trial reroutes the trace's ten hottest keys halfway through a
run and measures two TTL windows (`ch07::TtlLruCache`) against that
reroute, to show the hit-ratio versus staleness trade-off a TTL buys. A
third trial memoizes a pure dimension-classification function
(`ch07::classify_dimensions`) and compares it against calling that
function directly, to show why an unbounded, never-evicting cache is safe
for a pure function in a way it is not for `RouteResolver`.

## Layout

```
cpp/
├── include/ch07/
│   ├── route_resolver.hpp      # the slow backing lookup + reroute()
│   ├── zipf_trace.hpp          # the shared skewed access trace generator
│   ├── cache_result.hpp        # the {value, hit} shape every cache returns
│   ├── lru_cache.hpp           # LRU: std::list + unordered_map
│   ├── fifo_cache.hpp          # FIFO: std::deque + unordered_map
│   ├── lfu_cache.hpp           # LFU: frequency buckets, O(1)
│   ├── sieve_cache.hpp         # SIEVE: visited bit + moving hand
│   ├── ttl_lru_cache.hpp       # LRU + a TTL expiry check
│   ├── memoizer.hpp            # unbounded cache of a pure function
│   ├── dimension_classifier.hpp
│   └── cache_trial.hpp         # the per-lane trial runner + result shape
├── src/
│   ├── route_resolver.cpp
│   ├── zipf_trace.cpp
│   ├── dimension_classifier.cpp
│   ├── cache_trial.cpp
│   └── main.cpp                # runs every lane, writes results/caching.txt
└── tests/                      # GoogleTest for every policy's exact eviction behavior
results/
└── caching.txt                 # every lane's hit ratio, latency distribution, and correctness check
demo.sh                         # build -> run -> show the result
```

## How to drive it

```bash
cd examples/07-caching
./demo.sh            # build the default (no-sanitizer) lane, run, print the result
./demo.sh build       # just build
./demo.sh run         # just run (needs a prior build)
```

`demo.sh run` pipes `scripts/profile.sh`'s host-profile line into the
example binary, which runs every lane over its trace and copies the
profile header plus every lane's distribution into
`results/caching.txt`. The binary wraps the results directory in
`measure::ResultsDirScrubGuard`, the same host-id scrub every prior
chapter's example uses; `python3 -I scripts/validate.py` is the gate that
fails a commit on a leak.

## Behavioral verification

- `<lane>_mismatch_count` reads `0` for the uncached baseline and all four
  eviction-policy lanes: every served chute is checked against
  `RouteResolver::ground_truth` at the moment of that access, so a cache
  can be faster but never serve a different answer than the backing table
  would give right then.
- `ttl_short_stale_serve_count` and `ttl_long_stale_serve_count` are
  expected to be nonzero after the simulated reroute; that is the
  staleness this trial is measuring, not a defect. The short-TTL lane
  should show far fewer stale serves than the long-TTL lane for a small
  hit-ratio cost.
- `dimension_memo_mismatch_count` reads `0`: the memoized classifier
  returns the identical `SizeClass` as calling `classify_dimensions`
  directly, for every call in the trace.
- The GoogleTest suite checks each policy's exact eviction victim on a
  small, hand-traced sequence (see `tests/sieve_cache_test.cpp` for the
  trace that tells SIEVE apart from plain FIFO), the TTL cache's expiry
  boundary, and that the memoizer never diverges from calling the wrapped
  function directly.

## What to look for

- `sieve_hit_ratio` is the highest of the four policies on this trace, and
  `fifo_hit_ratio` is the lowest: FIFO ignores access entirely, so a hot
  key evicted only because something newer arrived has to pay the full
  backing-resolver cost to come back.
- `lfu_p50_us` is the slowest median of the four cached lanes despite a
  strong hit ratio, because its per-hit frequency-bucket move costs more
  than LRU's or SIEVE's O(1) list splice; `lfu_p999_us` is nonetheless the
  best-behaved tail of the four, because that same bookkeeping keeps truly
  cold keys out longer.
- `uncached_p50_us` sits far above any cached lane's `*_p50_us` (every
  access pays the full linear-scan cost); the gap narrows sharply at
  `*_p999_us`, where a cache miss costs the same as the uncached path and
  misses dominate the tail.
- `ttl_long_stale_serve_count` is far larger than `ttl_short_stale_serve_count`
  for a `ttl_long_hit_ratio` only marginally higher than
  `ttl_short_hit_ratio`: a short TTL buys most of its staleness reduction
  for a small hit-ratio cost.
- `dimension_memo_p50_us` sits far below `dimension_direct_p50_us`, and
  `dimension_memo_hit_ratio` is near 1.0 after the first pass through the
  small, fixed set of dimension tuples the trace draws from.

## Verification status

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane; `ctest --preset default` passes
(119 tests total across every chapter, including this chapter's 33 new
GoogleTests). The ASan/UBSan lane (`cmake --preset asan-ubsan`) builds
clean, its own `ctest --preset asan-ubsan` passes, and the example binary
runs clean under that lane (no sanitizer report). This example is
single-threaded, so there is no TSan lane for it. `./demo.sh` was run on
an unprivileged, untuned development host and produced
`results/caching.txt` with SIEVE winning hit ratio over the other three
policies, every `mismatch_count` at `0`, and both TTL lanes showing a
nonzero `stale_serve_count` after the simulated reroute, which is the
effect this chapter claims. `python3 -I scripts/validate.py` passes
(host-ids, distribution, harness-cap). The exact numbers are specific to
the host that produced them (see the profile header in
`results/caching.txt`) and are not a cross-machine benchmark; a re-run on
the same host will differ slightly from the numbers quoted in the chapter
text, since every lane measures real wall-clock cost on a shared, untuned
scheduler.*
