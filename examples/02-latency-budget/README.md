# Chapter 2 example: the latency budget

This example gives every one of the five stages (checkweigh, dimension,
scan, label, sort) a real, timed cost for the first time (see
`examples/01-latency-as-a-distribution/`, which only loaded the scan stage).
It records each stage's own distribution, sums scan, label, and sort into a
`scan_to_divert` lane (the three stages that run at or after the scanner, the
ones the physical scan-to-divert deadline actually bounds), and computes two
modelling outputs, Little's Law and Amdahl's Law, from those same
measurements.

## Layout

```
cpp/
├── include/ch02/
│   ├── pipeline_timing.hpp    # times every stage, sums scan+label+sort
│   ├── stage_workloads.hpp    # the five real per-stage workloads
│   └── budget_model.hpp       # compute_mean, Little's Law, Amdahl's Law
├── src/
│   ├── pipeline_timing.cpp
│   ├── stage_workloads.cpp
│   ├── budget_model.cpp
│   └── main.cpp                # runs the line, writes results/budget-decomposition.txt
└── tests/                      # GoogleTest for the harness, the workloads, and the model math
results/
└── budget-decomposition.txt    # the measured per-stage distributions and the model_* lines
demo.sh                         # build -> run -> show the result
```

## How to drive it

```bash
cd examples/02-latency-budget
./demo.sh            # build the default (no-sanitizer) lane, run, print the result
./demo.sh build       # just build
./demo.sh run         # just run (needs a prior build)
```

`demo.sh run` pipes `scripts/profile.sh`'s host-profile line into the example
binary, which copies that header into `results/budget-decomposition.txt` and
appends six distributions (checkweigh, dimension, scan, label, sort, and
scan_to_divert), each with its sample count, `p50_us`/`p99_us`/`p999_us`, and
an ASCII histogram, followed by the `model_little_law_*` and `model_amdahl_*`
lines. The binary wraps the results directory in
`measure::ResultsDirScrubGuard`, the same host-id scrub every prior chapter's
example uses; `python3 -I scripts/validate.py` is the gate that fails a
commit on a leak.

## What to look for

- `scan_p50_us` sits an order of magnitude above every other stage's
  `p50_us`: the barcode decode is this chapter's heaviest single cost, by
  design (see `ch02::scan_work`).
- `scan_to_divert_p50_us` is close to, but not identical to, `scan_p50_us`
  plus `label_p50_us` plus `sort_p50_us`: percentiles do not add the way raw
  samples do (`p50(A) + p50(B) != p50(A + B)` in general), which is exactly
  why `scan_to_divert_sample_count` and its own percentiles are computed from
  the summed per-parcel samples, never derived by adding the three stages'
  percentiles after the fact.
- Every `scan_to_divert_*_us` figure sits many orders of magnitude under
  `scan_to_divert_deadline_us` (750000.000): this chapter's workload, even
  with every stage doing real work, still leaves most of the physical budget
  unspent.
- `model_amdahl_parallel_fraction` is close to `model_amdahl_scan_mean_us`
  divided by `model_amdahl_scan_to_divert_mean_us`, by construction: scan is
  the heaviest stage inside the scan-to-divert window, so it is also the
  largest share Amdahl's Law has to work with.
- `model_little_law_loaded_L_estimate` (built from the same 2000
  parcels/second the distribution chapter's example stress-tested the scan
  stage with) is two orders of magnitude above
  `model_little_law_default_L_estimate`: a model can expose a rate that
  makes no physical sense for this belt well before any code runs.

## Verification status

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane; `ctest --preset default` passes,
including this chapter's new `PipelineTimingHarness`, stage-workload, and
`budget_model` GoogleTests. The
ASan/UBSan lane (`cmake --preset asan-ubsan`) builds clean, its own
`ctest --preset asan-ubsan` passes, and the example binary runs clean under
that lane (no sanitizer report). `./demo.sh` was run on an unprivileged,
untuned development host and produced
`results/budget-decomposition.txt` with scan as the dominant per-stage cost
and every `scan_to_divert_*_us` figure far under the 750 millisecond
deadline, which is the effect this chapter claims. `python3 -I
scripts/validate.py` passes (host-ids, distribution, harness-cap). The exact
numbers are specific to the host that produced them (see the profile header
in `results/budget-decomposition.txt`) and are not a cross-machine
benchmark; a re-run on the same host will differ slightly from the numbers
quoted in the chapter text, since the workloads measure real wall-clock
cost on a shared, untuned scheduler.*
