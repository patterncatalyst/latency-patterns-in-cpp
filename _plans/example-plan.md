---
title: "Example plan"
layout: plan
render_with_liquid: false
---

# Example plan

One row per chapter: the pattern, the one shipping example, its measured
lanes, the expected effect, and where it runs. See `_plans/v1-outline.md` for
the full chapter table this is drawn from.

| Ch | Pattern | Shipping example | Measured lanes | Expected effect | Host/VM |
|---|---|---|---|---|---|
| 0 | Running example and harness | Line-simulator past checkweigh, dimension, scan, label, sort | Baseline scan-to-divert, p50/p99/p99.9, histogram/eCDF | Baseline, not a comparison | Host |
| 1 | Latency as a distribution | Same line, measured naively then corrected | Mean versus distribution; corrected versus uncorrected for coordinated omission | Large, always visible | Host |
| 2 | The latency budget | Deadline modelled from belt speed and scan-to-diverter distance | Little's Law in-flight-parcel estimate versus measured; per-stage decomposition | Modelling, always visible | Host |
| 3 | Eliminating work | Scan decode path | Per-parcel alloc/lookup/recompute versus precomputed divert table with O(1) lookup | Large | Host |
| 4 | Zero-copy data layout | Parcel record and scan/divert thread handoff | AoS versus SoA; arena versus heap; shared-false versus fixed false sharing; NUMA-local versus remote (exploratory) | Medium-large; NUMA sub-part exploratory | Host |
| 5 | Wait-free synchronization | Scan-to-divert handoff | SPSC ring versus mutex queue, C++ and Rust tabs | Large at the tail under contention | Host |
| 6 | Exploiting concurrency | Sort-decision fan-out across diverter lanes | Data-parallel batch versus task-parallel per-parcel, C++ and Rust tabs | Medium-large (throughput at fixed SLO) | Host |
| 7 | Caching | Barcode-to-route cache in front of the routing table | LRU, LFU, FIFO, SIEVE lanes; TTL and memoization as sections | Large | Host |
| 8 | Shard-per-core partitioning | In-flight parcel tracker, partitioned by barcode hash | Unsharded versus shard-per-core; induced hot-chute skew versus rebalanced | Medium | Host |
| 9 | Asynchronous processing | Label-print, audit, and telemetry moved off the hot path | Synchronous versus event-loop/io_uring; unbatched versus batched; no backpressure versus backpressure, C++ and Rust tabs | Large | Host |
| 10 | Taming the host | Hot path run under jitter | Untuned versus isolcpus/nohz_full/pinned/RT-scheduled/mlocked; cyclictest before and after | Large at p99.9; VM-only, non-comparable to host numbers | VM-lab |
| 11 | Catalog index | No new example; indexes the eleven above | n/a | n/a | Host |
