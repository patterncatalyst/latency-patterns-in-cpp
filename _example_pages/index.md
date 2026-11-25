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
