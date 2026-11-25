# Chapter 4 example: zero-copy data layout

This example measures two unrelated effects, both about where bytes live
rather than how many instructions touch them.

**Ledger layout.** The scan stage keeps a 256-entry window of recently
scanned parcels, to flag a repeated scan key (a likely misfeed or
duplicate read) for manual review. `ch04::ParcelLedgerBefore` stores that
window as a ring of `std::unique_ptr<ParcelRecordBefore>`, each record a
wide struct with cold fields interleaved between the two hot ones the
check reads. `ch04::ParcelLedgerAfter` stores the same window as a
contiguous `std::vector<int>` of hot scan keys, with cold fields in a
separate, untouched store. Both lanes run over the identical seeded
5000-parcel sequence (`LineConfig::random_seed` defaults to `1`) and
resolve the identical divert decision for every parcel.

**False sharing.** Two threads, one standing in for the scan stage and one
for the divert stage, each increment their own counter 10,000,000 times
per trial, 30 trials. `ch04::UnpaddedCounters` places both counters inside
one 64-byte cache line; `ch04::PaddedCounters` aligns each counter to its
own line with `std::hardware_destructive_interference_size`. Both layouts
produce the identical final counts; only the time to get there differs.

## Layout

```
cpp/
├── include/ch04/
│   ├── parcel_ledger_before.hpp  # AoS: ring of pointers to wide, interleaved records
│   ├── parcel_ledger_after.hpp   # SoA: contiguous hot column, cold fields set apart
│   ├── layout_scan_stage.hpp     # the scan key and the two scan-stage lanes
│   ├── resolution_probe.hpp      # records the scan-to-divert span AND the resolved chute
│   └── false_sharing.hpp         # UnpaddedCounters, PaddedCounters, the two-thread trial
├── src/
│   ├── parcel_ledger_before.cpp
│   ├── parcel_ledger_after.cpp
│   ├── layout_scan_stage.cpp
│   ├── false_sharing.cpp
│   └── main.cpp                  # runs both lanes, writes results/zero-copy-layout.txt
└── tests/                        # GoogleTest for both correctness claims
results/
└── zero-copy-layout.txt          # ledger and false-sharing distributions, both lanes
demo.sh                           # build -> run -> show the result
```

## How to drive it

```bash
cd examples/04-zero-copy-layout
./demo.sh            # build the default (no-sanitizer) lane, run, print the result
./demo.sh build       # just build
./demo.sh run         # just run (needs a prior build)
```

`demo.sh run` pipes `scripts/profile.sh`'s host-profile line into the
example binary, which runs the ledger lanes, then the false-sharing
lanes, and copies the profile header plus all four distributions into
`results/zero-copy-layout.txt`. The binary wraps the results directory in
`measure::ResultsDirScrubGuard`, the same host-id scrub every prior
chapter's example uses; `python3 -I scripts/validate.py` is the gate that
fails a commit on a leak.

The false-sharing lane pins its two worker threads to distinct physical
cores by reading `/sys/devices/system/cpu/cpu*/topology/core_id` (no
privilege required) and calling `sched_setaffinity` on itself; if the
topology cannot be read, the trial runs unpinned rather than guessing a
host-specific CPU number. This affects only this process's own threads
and never touches host configuration.

## Behavioral verification

`chute_mismatches` in the results file counts how many of the 5000
compared parcels the two ledger layouts disagreed on. It should always
read `0`: both ledgers resolve a chute from the identical scan key and the
identical duplicate-window check, one through a ring of pointers, the
other through a contiguous array. `before_review_count` and
`after_review_count` should match exactly, since they count the same
flagged parcels under each layout. The GoogleTest suite
(`tests/layout_scan_stage_test.cpp`) checks the same property directly,
across 2000 barcodes and an engineered repeat, without a full
line-simulator run.

`unpadded_count_mismatches` and `padded_count_mismatches` should both read
`0`: every trial's two counters land on exactly
`false_sharing_iterations_per_thread`, regardless of which layout backs
them. `tests/false_sharing_test.cpp` checks this directly, plus that
padded and unpadded trials always agree on the final count across
repeated runs.

## What to look for

- `layout_before_p50_us` sits roughly twice `layout_after_p50_us`:
  the ring-of-pointers ledger's wide, interleaved records cost more cache
  lines per duplicate check than the struct-of-arrays ledger's packed hot
  column, with no allocation difference between the two lanes to confound
  the comparison.
- `unpadded_p50_us` sits several times above `padded_p50_us`: two
  counters sharing a 64-byte cache line cost real time to keep coherent
  across two cores, even though neither thread ever reads or writes the
  other's variable.
- `chute_mismatches`, `unpadded_count_mismatches`, and
  `padded_count_mismatches` all read `0`: neither layout change in this
  example altered a single decision or a single count, only how long each
  one took to reach.

## Verification status

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane; `ctest --preset default` passes (62
tests total, including this chapter's new `layout_scan_stage` and
`false_sharing` GoogleTests). The ASan/UBSan lane (`cmake --preset
asan-ubsan`) builds clean, its own `ctest --preset asan-ubsan` passes, and
the example binary runs clean under that lane (no sanitizer report).
Because this example is multithreaded, the TSan lane (`cmake --preset
tsan`) was also built and run directly against the example binary, with
no data-race report. `./demo.sh` was run on an unprivileged, untuned
development host and produced `results/zero-copy-layout.txt` with the
ledger's before lane consistently slower than the after lane and the
unpadded counters consistently slower than the padded counters, which is
the effect this chapter claims, plus `chute_mismatches=0` and both
`*_count_mismatches` fields at `0`. `python3 -I scripts/validate.py`
passes (host-ids, distribution, harness-cap). The exact numbers are
specific to the host that produced them (see the profile header in
`results/zero-copy-layout.txt`) and are not a cross-machine benchmark; a
re-run on the same host will differ slightly, and the false-sharing ratio
in particular varies run to run with how the scheduler interleaves two
busy threads.*
