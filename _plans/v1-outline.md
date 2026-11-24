---
title: "v1 outline"
layout: plan
render_with_liquid: false
---

# v1 outline

v1 is frozen at 12 chapters (Ch0 through Ch11, including the catalog index).
Spine metric everywhere: scan-to-divert (sense, decide, act). Every "measured"
cell reports p50/p99/p99.9, never a mean.

## Example model

One umbrella pattern per chapter, one running shipping-domain example per
chapter, with sub-techniques as measured comparison lanes inside that one
example. Ch7's caching chapter, for instance, is one cache-in-front-of-routing
example measured across LRU, LFU, FIFO, and SIEVE lanes; TTL and memoization
are sections of that chapter, not separate projects. One automated walkthrough
and one profile-labelled `results/` directory per chapter, lanes within it.
Not several example projects per chapter, and not a split of the fat
chapters. This keeps v1 at 12 chapters and reads like a pattern catalog.

## The 12 chapters

| # | Pattern / technique | Shipping example: what runs, what's measured | Rust? | Host/VM | Effect + flag |
|---|---|---|---|---|---|
| 0 | Running example and harness | Line-simulator past checkweigh, dimension, scan, label, sort; defines scan-to-divert; harness emits p50/p99/p99.9 plus histogram/eCDF | No | Host | Baseline |
| 1 | Latency as a distribution (coordinated omission) | Same line; shows the mean lies; the belt never waits, so the measurement corrects for coordinated omission | No | Host | Large, always visible |
| 2 | The latency budget (Little's/Amdahl modelling; sources; compounding) | Deadline = distance / belt_speed; Little's Law for in-flight parcels; per-stage decomposition | No | Host | Modelling; always visible |
| 3 | Eliminating work (complexity, ser/de, memory management, precomputation) | Scan decode path: per-parcel alloc, map lookup, and recompute, replaced with a precomputed divert table and an O(1) lookup | No | Host | Large |
| 4 | Zero-copy data layout (memory topology, cache locality, false sharing) | Parcel record AoS to SoA, arena layout, dropped in-process serialization, fixed false sharing between scan/divert threads, NUMA-local | Prose sidebar only | Host | Medium-large; NUMA sub-part exploratory on a single-socket laptop |
| 5 | Wait-free synchronization (atomics, memory ordering, SPSC ring) | Scan-to-divert handoff via an SPSC ring versus a mutex queue; acquire/release ordering | Yes | Host | Large at the tail under contention |
| 6 | Exploiting concurrency (threads, coroutines, actors; data versus task) | Fan the sort decision across diverter lanes; data-parallel batch versus task-parallel per-parcel | Yes | Host | Medium-large (throughput at a fixed SLO) |
| 7 | Caching (LRU/LFU/FIFO/SIEVE, hit ratio, TTL, memoization, materialized views) | In-process barcode-to-route cache in front of the routing table; replacement-policy tails; TTL; memoize dimension-to-class | No | Host | Large; distributed coherency deferred to v2 |
| 8 | Shard-per-core partitioning (horizontal/vertical, routing, skew) | In-flight parcel tracker partitioned by barcode hash across shard-per-core workers; induce hot-chute skew; rebalance | No | Host | Medium; single-box reframe |
| 9 | Asynchronous processing (event loop, async I/O, batching, backpressure, pools, off-path observability) | Move label-print, audit, and telemetry off the hot path via an event loop and io_uring; batch label writes; apply backpressure when the printer lags; buffer pool | Yes | Host | Large |
| 10 | Taming the host (OS overhead, demand paging, jitter, CPU isolation, RT) | Run the hot path under jitter; isolcpus/nohz_full, pinning, RT scheduling, mlock; cyclictest before and after | No | VM-lab | Large at p99.9; VM-only numbers |
| 11 | Catalog index | Pattern catalog: each pattern mapped to its scan-to-divert budget line, when to reach for it, host or VM, and its effect | No | Host | n/a |

## v2 backlog (deferred, not built)

Predictive techniques (prefetch, optimistic updates, speculative execution,
hedging); replication and consistency models; distributed cache coherency;
internode colocation and the network-stack deep-dive; kernel-bypass transport
(AF_XDP/DPDK in-VM); a transport-by-latency-budget protocol survey; a
workshop spin-off. Reason: each needs a cluster or a real NIC to measure, or,
for the predictive techniques, has an effect too small to see on an untuned
laptop.
