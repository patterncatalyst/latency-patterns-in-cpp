# Chapter 0 example: the running example and the harness

This is the baseline example every later chapter compares against. It drives
the shared parcel sortation line (`examples/common/line-simulator/`) at its
house defaults, records scan-to-divert with the shared measurement harness
(`examples/_lib/`), and writes a profile-labelled results file. No
optimization technique is applied here: this chapter teaches how the
measurement works, not how to speed anything up.

## Layout

```
cpp/
├── include/ch00/recording_probe.hpp   # adapts sense/decide/act to the recorder
├── src/main.cpp                       # runs the line, writes results/baseline.txt
└── tests/recording_probe_test.cpp     # GoogleTest for the adapter
results/
└── baseline.txt                       # the measured distribution (see below)
demo.sh                                # build -> run -> show the baseline
```

## How to drive it

```bash
cd examples/00-running-example
./demo.sh            # build the default (no-sanitizer) lane, run, print the baseline
./demo.sh build       # just build
./demo.sh run         # just run (needs a prior build)
```

`demo.sh run` pipes `scripts/profile.sh`'s host-profile line into the example
binary, which copies that header into `results/baseline.txt` and appends the
measured scan-to-divert distribution: `p50_us`, `p99_us`, `p999_us`, and an
ASCII histogram. The binary wraps the results directory in
`measure::ResultsDirScrubGuard`, so every file it writes is passed through
`scripts/scrub-host-ids.py` before the process exits; `python3 -I
scripts/validate.py` is the gate that actually fails a commit on a leak.

## What to look for

- `sample_count=1000` in `results/baseline.txt`: one sample per parcel, no
  coordinated-omission backfill, because this run passes no expected interval
  to `LatencyRecorder` yet (see the chapter's next installment for why that
  changes).
- `p50_us`, `p99_us`, and `p999_us` are all far under
  `scan_to_divert_deadline_us` (750,000 us at the house default of a
  2.0 m/s belt and a 1.5 m scan-to-diverter distance): the baseline line has
  enormous headroom before any technique is needed. Every later chapter
  narrows that headroom's relevance by making the workload itself heavier
  (more lookup work, more contention, more work moved onto the hot path);
  this chapter only establishes that the budget and the measurement exist.
- The histogram's mass sits in one narrow bucket near the minimum, with a
  long near-empty tail out to `p999_us`: the shape a reader should expect
  from an unloaded, synchronous loop with no contention and no I/O.

## Verification status

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane, `ctest --preset default` passes
(including this example's GoogleTest for the recording adapter), and
`./demo.sh` was run on an unprivileged, untuned development host: it produced
`results/baseline.txt` with a non-empty distribution whose percentiles sit
below the configured scan-to-divert deadline, which is the effect this
chapter claims. `python3 -I scripts/validate.py` passes (host-ids,
distribution, harness-cap). The exact numbers are specific to the host that
produced them (see the profile header in `results/baseline.txt`) and are not
a cross-machine benchmark.*
