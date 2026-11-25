---
title: "Outline"
order: -1  # before Chapter 0; keeps the TOC page's order from colliding with a real chapter number
part: "Measuring Latency"
description: "The twelve chapters of v1, the running example, and what each one measures."
---

This guide has twelve chapters. Each one takes a single latency pattern,
applies it to one running example, and measures the result with the same
metric: scan-to-divert, the time from sensing a parcel to acting on it.

The running example is a parcel sortation line: a parcel moves through
checkweigh, dimension, scan, label, and sort. The belt never waits for a slow
consumer, so coordinated omission is physical here, not an abstraction.
Chapters 5, 6, and 9 add a Rust lane next to the C++23 lane in a tabbed code
block, where ownership changes what the technique has to prove. Chapter 10
runs in a throwaway virtual machine; every other chapter runs unprivileged, on
the host it is written on.

| Part | Chapter | Pattern |
|---|---|---|
| Measuring Latency | 0 | Running example and harness |
| Measuring Latency | 1 | Latency as a distribution |
| Measuring Latency | 2 | The latency budget |
| Eliminating Work | 3 | Eliminating work |
| Eliminating Work | 4 | Zero-copy data layout |
| Concurrency and Synchronization | 5 | Wait-free synchronization |
| Concurrency and Synchronization | 6 | Exploiting concurrency |
| Concurrency and Synchronization | 9 | Asynchronous processing |
| Scaling and the Host | 7 | Caching |
| Scaling and the Host | 8 | Shard-per-core partitioning |
| Scaling and the Host | 10 | Taming the host |
| Scaling and the Host | 11 | Catalog index |

Every number in this guide carries a label: which machine or VM profile
produced it, never a mean on its own. See the
[iteration plan]({{ '/plans/iteration-plan/' | relative_url }}) for the order
chapters ship in.
