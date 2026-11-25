---
title: "Premise probe"
layout: plan
render_with_liquid: false
---

# Premise probe

Before any lane or harness gets built around a technique, r00/r01 measure its
effect size on this host with a quick, throwaway probe. A technique whose
effect disappears into noise on an untuned laptop gets flagged `exploratory`
here, not hidden from the chapter. Fill in the `Measured effect` and `Flag`
columns as each probe runs; `TBD` means not yet measured.

| Ch | Technique | Probe (what gets run) | Measured effect | Flag |
|---|---|---|---|---|
| 0 | Baseline scan-to-divert (no technique; the number every later probe compares against) | `examples/00-running-example` at the house default config (2.0 m/s belt, 1.5 m scan-to-diverter distance, 1000 parcels, no coordinated-omission correction) | p50=0.055us, p99=0.076us, p99.9=0.543us; deadline=750000.000us; measured on a 16-core/22-thread laptop CPU, powersave governor, bare metal, no pinning/isolation (see `examples/00-running-example/results/baseline.txt`) | baseline (not an A/B probe) |
| 1 | Mean versus distribution under load | Line-simulator at a fixed rate, compare a time-averaged latency figure to p50/p99/p99.9 of the same run | TBD | TBD |
| 3 | Precomputed divert table versus per-parcel recompute | Scan decode path, A/B the two lookup strategies at the same input rate | TBD | TBD |
| 4 | AoS versus SoA parcel record | Same scan/divert loop, swap only the record layout | TBD | TBD |
| 4 | False-sharing fix between scan and divert threads | Same two threads, padded versus unpadded shared counters | TBD | TBD |
| 4 | NUMA-local versus remote allocation | Single-socket laptop; likely invisible without a second socket | TBD | exploratory (expected) |
| 5 | SPSC ring versus mutex queue | Same handoff, swap only the queue, measure the tail under contention | TBD | TBD |
| 6 | Data-parallel batch versus task-parallel per-parcel | Same sort-decision fan-out, swap only the scheduling strategy | TBD | TBD |
| 7 | Cache replacement policy (LRU/LFU/FIFO/SIEVE) | Same barcode-to-route cache, swap only the eviction policy, fixed working set | TBD | TBD |
| 8 | Shard-per-core versus unsharded tracker | Same in-flight parcel tracker, swap only the partitioning strategy, induce hot-chute skew | TBD | TBD |
| 9 | Off-path observability versus inline instrumentation | Same hot path, move telemetry off-path via an event loop | TBD | TBD |
| 10 | Host jitter versus isolated/RT-scheduled | VM-lab only; cyclictest before and after, not comparable to any host number in this table | TBD | VM-only |

A probe that comes back indistinguishable from noise does not get a lane; it
gets a sentence in the chapter explaining why, and the `exploratory` flag
stays in `_plans/v1-outline.md` next to that chapter's effect column.
