---
title: "Iteration plan"
layout: plan
render_with_liquid: false
---

# Iteration plan

Each iteration is its own `release/rNN` branch and PR, squash-merged into
`main`, then tagged `rNN.x`. v1 ships Ch0 through Ch11 plus the two decks,
in this order:

| rNN | Contents | Skills |
|---|---|---|
| r00 | Scaffold (repo, site, toolchain), CLAUDE.md, `_plans/*`, line-simulator, premise probe | github, jekyll, cpp, diagram |
| r01 | Ch0 (running example and harness) + Ch1 (distribution); spine premise proven | tutorial, cpp, diagram |
| r02 | Ch2 (latency budget) + Ch3 (eliminating work) | tutorial, cpp |
| r03 | Ch4 (zero-copy data layout) + Ch5 (wait-free sync, first Rust lane) | tutorial, cpp, idiomatic-review |
| r04 | Ch6 (concurrency, Rust lane) + Ch7 (caching) | tutorial, cpp, idiomatic-review |
| r05 | Ch8 (shard-per-core) + Ch9 (async, Rust lane, off-path observability) | tutorial, cpp, docker-stack, idiomatic-review |
| r06 | Ch10 (taming the host), VM-lab, `LAB_PREFIX=lowlat` | systems-programming, tutorial |
| r07 | Ch11 (catalog index) + cross-reference cleanup (name the pattern, not a chapter number) | tutorial, diagram |
| r08 | Polish: voice pass, idiomatic review, scrub and validate hardening | professional-voice, idiomatic-review, host-scrub |
| r09 | Decks 101 and 201, last | presentation |

## VM-lab versus host-only

Ch0 through Ch9, and Ch11, run host-only: unprivileged user space, no host
changes. Ch10 (taming the host) is the one VM-lab chapter: a throwaway
KVM/libvirt `lowlat-*` VM, reverted to snapshot between runs, every number
VM-labelled. v2 kernel-bypass and colocation work is VM-lab or real-NIC, and
deferred past v1. Eleven of the twelve v1 chapters never touch the lab; only
Ch10 does, landing at r06.

## Rust-comparison map

Rust appears as a tabbed side-by-side in exactly Ch5, Ch6, and Ch9. Ch4 gets a
prose ownership sidebar only, no code.

- **Ch5**, wait-free synchronization: atomics and memory ordering map closely
  between the two languages, but Rust's `Send`/`Sync` and ownership forbid at
  compile time the race the C++ SPSC ring can hit at runtime.
- **Ch6**, exploiting concurrency: Rust's ownership makes the data-parallel
  versus task-parallel split safe without the lock the C++ version needs.
- **Ch9**, asynchronous processing: the async models differ in a way worth
  teaching (Rust `async`/`await` and executors versus C++23 coroutines and
  executors), and that difference is the lesson.

No other chapter carries a Rust lane. A lane in Ch0 through Ch4 (other than
Ch4's sidebar), Ch7, Ch8, Ch10, or Ch11 would be noise: host tuning and the
catalog index carry no ownership lesson to teach.

## r01 housekeeping from the r00 validation pass

The r00 scaffold passed validation with no blocking findings. Fold these
low-severity items into r01 before the work they touch:

- **CMake preset toolchain (do before any asan-only build).** The `asan-ubsan`,
  `tsan`, and `release` presets inherit `toolchainFile` from `conan-base`
  pointing at `build/conan_toolchain.cmake`. They build when the `default` lane
  ran first, but a fresh checkout that configures only `asan-ubsan` has no
  `build/` conan install and fails. Give each lane its own `conan install -of
  <dir>` plus a per-lane `toolchainFile`, or document a single required
  `conan install -of build` before any preset.
- **Chapter numbering versus reading order.** `_docs/00-outline.md` groups
  Ch9 (asynchronous) under the concurrency part while Ch7 and Ch8 sit under the
  scaling part, so prev/next nav reads 5, 6, 9, 7, 8, 10. Settle number versus
  order before chapters ship.
- **LatencyRecorder::percentile() sorts per call** (`examples/_lib/src/
  latency_recorder.cpp`): computing p50/p99/p99.9 sorts three times. Harmless
  at current sample sizes; cache a sorted view if it shows up in a probe.
- **CLAUDE.md voice-scanner false positive (cosmetic).** The voice scanner
  flags CLAUDE.md's own list of banned reassurance words. No prose fix; add a
  scanner ignore for CLAUDE.md if the noise is bothersome.

r01 status: the pages.yml PR-safe split, the per-lane sanitizer toolchain,
and the new `ci.yml` summary check (items 1-3) shipped in r01. Chapter
numbering versus reading order, the per-call percentile sort, and the
CLAUDE.md voice-scanner false positive (the remaining three items above) are
still open, deferred to the iteration that touches each.

r03 status: the hermetic, pinned Rust toolchain (`scripts/bootstrap-rust.sh`,
`scripts/rust-env.sh`, `rust-toolchain.toml`) and its CI lane landed ahead of
the chapter content, so the wait-free synchronization chapter's Rust lane has
a toolchain to build against on day one.
