---
title: "Examples"
layout: plan
permalink: /examples/
description: "Every runnable example in this repo, mapped to the chapter it supports."
---

Every chapter in this guide ships one runnable example under
[`examples/`](https://github.com/{{ site.github_username }}/{{ site.github_repo }}/tree/main/examples)
in the repo, measured against the same parcel sortation line. This page fills
in one row per example as each chapter lands. See the
[iteration plan]({{ '/plans/iteration-plan/' | relative_url }}) for the order
chapters ship in.

| Example | What it shows | Chapter |
|---|---|---|
| [`00-running-example`](https://github.com/{{ site.github_username }}/{{ site.github_repo }}/tree/main/examples/00-running-example) | The parcel sortation line and the measurement harness; the uncorrected scan-to-divert baseline every later chapter compares against | [The running example and the harness]({{ '/docs/00-running-example-and-harness/' | relative_url }}) |
| [`01-latency-as-a-distribution`](https://github.com/{{ site.github_username }}/{{ site.github_repo }}/tree/main/examples/01-latency-as-a-distribution) | A loaded scan stage (real decode cost plus a periodic stall); the same run measured naively and with coordinated-omission correction, mean shown next to the percentiles | [Latency as a distribution]({{ '/docs/01-latency-as-a-distribution/' | relative_url }}) |
| [`02-latency-budget`](https://github.com/{{ site.github_username }}/{{ site.github_repo }}/tree/main/examples/02-latency-budget) | The per-stage decomposition of scan-to-divert, with Little's Law and Amdahl's Law computed as planning models from the measured run | [The latency budget]({{ '/docs/02-the-latency-budget/' | relative_url }}) |
| [`03-eliminating-work`](https://github.com/{{ site.github_username }}/{{ site.github_repo }}/tree/main/examples/03-eliminating-work) | The scan decode path before and after: per-parcel allocation, a hash-map lookup, and a rebuild replaced by a precomputed divert table with O(1) resolution | [Eliminating work]({{ '/docs/03-eliminating-work/' | relative_url }}) |
| [`04-zero-copy-layout`](https://github.com/{{ site.github_username }}/{{ site.github_repo }}/tree/main/examples/04-zero-copy-layout) | The recent-scan ledger as a pointer ring versus a contiguous hot-field array, and two counters sharing a cache line versus split onto their own | [Zero-copy data layout]({{ '/docs/04-zero-copy-data-layout/' | relative_url }}) |
| [`05-wait-free-sync`](https://github.com/{{ site.github_username }}/{{ site.github_repo }}/tree/main/examples/05-wait-free-sync) | The scan-to-divert handoff as a mutex-guarded queue versus a wait-free SPSC ring, with an idiomatic Rust lane showing the compile-time safety guarantee | [Wait-free synchronization]({{ '/docs/05-wait-free-synchronization/' | relative_url }}) |
