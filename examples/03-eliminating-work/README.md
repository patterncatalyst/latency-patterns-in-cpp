# Chapter 3 example: eliminating work

This example measures the scan decode path twice, over the exact same
seeded sequence of 5000 parcels (`LineConfig::random_seed` defaults to `1`
and neither run changes it). The "before" lane (`ch03::BeforeScanStage`)
resolves each parcel's divert chute the unoptimized way: a per-parcel heap
allocation, a `std::unordered_map` lookup, and a from-scratch rebuild of a
small lookup table that never changes. The "after" lane
(`ch03::AfterScanStage`) resolves the identical chute with a precomputed
`std::array` lookup, no per-parcel allocation, and the small table built
once instead of 5000 times.

Both lanes share the one barcode-decode checksum (`ch03::decode_key_for_barcode`)
and the one manifest source (`ch03::manifest_chute_for_key`,
`ch03::manifest_zone_multiplier_for_chute`): what differs between them is
only how each one turns a decoded key into a divert decision, never what
decision it reaches.

## Layout

```
cpp/
├── include/ch03/
│   ├── divert_manifest.hpp    # the decode, the manifest source, the map and table builders
│   ├── resolution_probe.hpp   # records the scan-to-divert span AND the resolved chute
│   ├── before_scan_stage.hpp  # allocation + map lookup + recomputed table
│   └── after_scan_stage.hpp   # precomputed table, no allocation, hoisted lookup
├── src/
│   ├── divert_manifest.cpp
│   ├── before_scan_stage.cpp
│   ├── after_scan_stage.cpp
│   └── main.cpp                # runs both lanes, writes results/eliminating-work.txt
└── tests/                      # GoogleTest for the manifest math and before/after agreement
results/
└── eliminating-work.txt        # both measured distributions plus chute_mismatches
demo.sh                         # build -> run -> show the result
```

## How to drive it

```bash
cd examples/03-eliminating-work
./demo.sh            # build the default (no-sanitizer) lane, run, print the result
./demo.sh build       # just build
./demo.sh run         # just run (needs a prior build)
```

`demo.sh run` pipes `scripts/profile.sh`'s host-profile line into the
example binary, which runs the before lane, then the after lane, over the
same seeded parcel sequence, and copies the profile header plus both
distributions into `results/eliminating-work.txt`. The binary wraps the
results directory in `measure::ResultsDirScrubGuard`, the same host-id
scrub every prior chapter's example uses; `python3 -I scripts/validate.py`
is the gate that fails a commit on a leak.

## Behavioral verification

`chute_mismatches` in the results file counts how many of the 5000
compared parcels the two lanes disagreed on. It should always read `0`:
`BeforeScanStage` and `AfterScanStage` both derive their chute from the
same `ch03::manifest_chute_for_key` source (one builds a
`std::unordered_map` from it, the other a `std::array`), and both apply the
same oversize-for-weight check from the same
`ch03::manifest_zone_multiplier_for_chute` source. The GoogleTest suite
checks the same property directly against the two stage objects, across
2000 distinct barcodes and an explicit oversize case, without going through
a full line-simulator run (see `tests/before_after_divert_test.cpp`).

## What to look for

- `before_p50_us` sits several times above `after_p50_us`: removing a
  64-byte heap allocation, a hash-map lookup, and a 12-entry vector rebuild
  from a per-parcel path is a large, consistently visible effect on this
  host, not a marginal one.
- `before_p999_us` sits further above `after_p999_us`, proportionally, than
  the two `p50_us` figures do: the before lane's occasional slow outliers
  (visible as a thin, scattered tail in its histogram) are consistent with
  an allocator occasionally doing more work than a fast-path `malloc`/`free`
  pair, something the after lane cannot exhibit because it never allocates
  on this path at all.
- `chute_mismatches` reads `0`: eliminating all three wastes never changed
  which chute a single one of the 5000 parcels was routed to.

## Verification status

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane; `ctest --preset default` passes (56
tests total across every chapter, including this chapter's new
`divert_manifest` and `before_after_divert` GoogleTests). The ASan/UBSan
lane (`cmake --preset asan-ubsan`) builds clean, its own
`ctest --preset asan-ubsan` passes, and the example binary runs clean under
that lane (no sanitizer report). `./demo.sh` was run on an unprivileged,
untuned development host and produced `results/eliminating-work.txt` with
the before lane several times slower than the after lane at every
percentile and `chute_mismatches=0`, which is the effect this chapter
claims. `python3 -I scripts/validate.py` passes (host-ids, distribution,
harness-cap). The exact numbers are specific to the host that produced them
(see the profile header in `results/eliminating-work.txt`) and are not a
cross-machine benchmark; a re-run on the same host will differ slightly
from the numbers quoted in the chapter text, since both lanes measure real
wall-clock cost on a shared, untuned scheduler.*
