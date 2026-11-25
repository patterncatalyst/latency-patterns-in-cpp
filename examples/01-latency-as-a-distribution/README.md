# Chapter 1 example: latency as a distribution

This example drives the shared parcel sortation line
(`examples/common/line-simulator/`) under a real workload for the first
time: every parcel pays a decode cost at the scan stage, and one parcel in
250 pays an 8 millisecond stall standing in for a GC-like pause or a
downstream stage (the label printer's spooler) briefly blocking the scan
stage. It records scan-to-divert twice from the same run: once the way a
generator that waits for each parcel to finish would report it (naive), and
once with Gil Tene's coordinated-omission correction turned on (corrected).

## Layout

```
cpp/
├── include/ch01/
│   ├── loaded_scan_stage.hpp      # the decode workload + periodic stall
│   ├── sample_collecting_probe.hpp  # adapts sense/decide/act to a raw sample vector
│   └── distribution_stats.hpp     # the naive mean + the naive/corrected recorder pair
├── src/
│   ├── distribution_stats.cpp
│   └── main.cpp                   # runs the line, writes results/loaded-distribution.txt
└── tests/                         # GoogleTest for the workload and the stats helpers
results/
└── loaded-distribution.txt        # the measured naive and corrected distributions
demo.sh                            # build -> run -> show the result
```

## How to drive it

```bash
cd examples/01-latency-as-a-distribution
./demo.sh            # build the default (no-sanitizer) lane, run, print the result
./demo.sh build       # just build
./demo.sh run         # just run (needs a prior build)
```

`demo.sh run` pipes `scripts/profile.sh`'s host-profile line into the example
binary, which copies that header into `results/loaded-distribution.txt` and
appends two distributions (naive and corrected), each with its sample count,
`p50_us`/`p99_us`/`p999_us`, and an ASCII histogram, plus `naive_mean_us`:
the one arithmetic mean this guide reports anywhere, included only to show
it next to the percentiles computed from the exact same samples. The binary
wraps the results directory in `measure::ResultsDirScrubGuard`, the same
host-id scrub the running example and harness chapter's example uses;
`python3 -I scripts/validate.py` is the gate that fails a commit on a leak.

## What to look for

- `naive_mean_us` sits tens of times above `naive_p50_us`: the rare 8
  millisecond stalls pull the average far above what nearly every parcel
  actually experiences, while `naive_p50_us` reports what the typical
  parcel saw.
- `naive_p99_us` stays close to `naive_p50_us`: at a 0.4% stall rate (one
  parcel in 250), the stall population is smaller than the top 1% a p99
  cutoff reaches, so the naive p99 can miss the stall outright.
  `naive_p999_us` (the top 0.1%) does catch it, since 0.4% is bigger than
  0.1%.
- `corrected_p99_us` sits orders of magnitude above `naive_p99_us`:
  backfilling the arrivals a stall implies were waiting behind it (see
  `measure::LatencyRecorder::record()`) moves enough mass into the tail that
  the corrected p99 lands inside the stall population the naive p99 missed.
  `corrected_p999_us` barely moves versus `naive_p999_us`, because the
  single worst stall was already the maximum in both distributions; the
  correction has less left to add at that extreme.
- `corrected_sample_count` is larger than `naive_sample_count` (more rows in
  the corrected recorder for the same run): that gap is the coordinated
  omission the naive measurement could not see.

## Verification status

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane; `ctest --preset default` passes,
including the new `LoadedScanStage` and `distribution_stats` GoogleTests. The
ASan/UBSan lane (`cmake --preset asan-ubsan`) builds clean, its own
`ctest --preset asan-ubsan` passes, and the binary itself runs clean under
that lane (no sanitizer report). `./demo.sh` was run on an unprivileged,
untuned development host and produced `results/loaded-distribution.txt`
with a naive mean far above the naive median, a naive p99 that misses the
stall, and a corrected p99 that moves into the stall population, which is
the effect this chapter claims. `python3 -I scripts/validate.py` passes
(host-ids, distribution, harness-cap). The exact numbers are specific to the
host that produced them (see the profile header in
`results/loaded-distribution.txt`) and are not a cross-machine benchmark.*
