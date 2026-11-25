---
title: "Eliminating work"
order: 3
part: "Eliminating Work"
description: "The scan decode path rebuilt without the allocation, the map lookup, and the recomputed table it never needed: the first chapter with a large, clearly measured optimization effect."
duration: 25 minutes
---

## Learning objectives

By the end of this chapter you can:

- Name three categories of per-parcel waste, a heap allocation, a hash-map
  lookup where a table would do, and a recomputed value that never changes,
  and say why each is a choice a program makes, not a cost a slow CPU
  forces onto it.
- Explain why a `std::unordered_map` and a precomputed `std::array` can
  carry the same average-case complexity class and still cost very
  differently on a hot path.
- Build a dense lookup table from a decision that used to run per call, and
  say when that trade (memory and setup time for a one-instruction lookup)
  is worth making.
- Read a measured before/after comparison and point to the one behavioral
  check that proves the optimization changed nothing about what the line
  decides.

## Diagram

{% include excalidraw.html file="eliminating-work-before-after" alt="Two stacked lanes showing the scan decode path. The before lane has four boxes in sequence: decode barcode, allocate and serialize a record, a hash-map lookup, and a rebuilt zone-multiplier table, ending in a divert decision, with its measured p50 of 0.596 microseconds. The after lane keeps the same decode box, replaces the three eliminated boxes with a single precomputed array index, and ends in the same divert decision, with its measured p50 of 0.116 microseconds." caption="Figure 3.1: The same decode, the same decision, three fewer things done to get there." %}

## Why this matters

The latency budget chapter gave every stage on the line a real, timed cost
for the first time and found that scan dominates every other stage's
contribution by an order of magnitude. It also named what it was not
doing: "a later chapter's 'eliminating work' replaces one of them with less
work, not less hardware." This is that chapter, and the barcode decode is
where it starts.

Every chapter so far has measured a cost; this is the first one that cuts
one. The pattern is older than this guide: the fastest work is the work a
program does not do. That is routinely ignored in practice, because the
work that should not happen rarely looks like waste from inside the
function that does it. A heap allocation looks like an implementation
detail. A hash-map lookup looks like the normal way to resolve a key. A
helper that rebuilds a twelve-entry table looks cheap, because twelve is a
small number. None of those three things is expensive alone. Paid once per
parcel on a line that never waits, all three add up to real, measurable
cost, and all three can be removed without changing a single thing the
line decides.

This chapter does not touch the barcode decode itself; the checksum loop
that turns a barcode into a routing key stays exactly as expensive in both
lanes measured below. What changes is everything a decode path does after
it has that key and before it has a chute.

The code is in `examples/03-eliminating-work/`; its `README.md` covers
what it does and how to drive it.

## The before path and why it is wasteful

A real sortation line does not route by the full barcode. A barcode
identifies one physical item; a divert decision only needs to know which of
a small, fixed set of SKU families that item belongs to, because the
family, not the barcode, is what maps to a chute.
`ch03::decode_key_for_barcode` models that step: the same
multiplicative-hash checksum loop the distribution and latency budget
chapters already use,
folded down into a dense key space of 4096 values. That decode cost is
identical in both lanes this chapter measures; what differs is what each
lane does with the key afterward.

`ch03::BeforeScanStage` is the realistic, unoptimized shape most decode
paths grow into by accretion, one reasonable-looking addition at a time. On
every parcel, after the decode, it does three things. It builds a
`std::vector<std::byte>`, writes the decode key into the first four bytes,
and reads those same four bytes straight back out, a heap-allocated round
trip nothing downstream needed: this decode path was written as "decode,
then serialize, then look up" instead of "decode, then look up." It looks
up the reconstructed key in a `std::unordered_map<int, int>`, built once at
startup from the same manifest the after lane uses, to resolve a chute. And
it rebuilds a twelve-entry `std::vector<double>` of per-chute priority
weights, calling `ch03::manifest_zone_multiplier_for_chute` twelve times on
every parcel, to compute a table whose values never change across the run;
that table feeds an oversize-for-weight check (the volumetric-weight idea
the distribution chapter introduced), diverting an oversize-for-its-weight
parcel to a dedicated overflow chute.

None of these three steps is dead code: the reconstructed key is the
actual map key, the map's answer is the actual chute until the oversize
check overrides it, and the rebuilt table is what that check reads. A
profiler pointed at this function finds three real costs doing
real-looking work, which is why this kind of waste survives code review:
every individual line is correct.

## The after path and what was eliminated

`ch03::AfterScanStage` reaches the identical divert decision with none of
the three costs above. The decode stays unchanged. Instead of a byte
buffer and a map, the decode key indexes directly into `divert_table_`, a
`std::array<int, key_space>` built once, in the constructor, from the same
`ch03::manifest_chute_for_key` source the before lane's map is built from.
Instead of a vector rebuilt on every parcel, `zone_multipliers_` is a
`std::array<double, chute_count>` also built once in the constructor. The
per-parcel path does exactly two things: index one array for a chute, and
a second, smaller array for that chute's weight.

Both lanes resolve from the same two source functions, which is what makes
the comparison fair rather than a race between two different answers.
`tests/before_after_divert_test.cpp` checks this directly: both stages run
over 2000 distinct barcodes, with no line-simulator involved, and every
one must produce the identical chute.

## What actually got eliminated

**Algorithmic complexity.** A `std::unordered_map::find` and a direct array
index carry the same average-case complexity class, O(1), and that
equality is not the lesson here: the constant work behind each O(1) differs
widely. An array index is one multiply and one memory load, landing in a
16-kilobyte table that fits a laptop CPU's L1 or L2 cache. A hash-map
lookup computes a hash, follows a pointer into a bucket, and compares keys,
each node typically living at an address the allocator handed out
independently of its neighbors, a worse access pattern than a flat array.
Two operations in the same Big-O class are not the same operation; the
constant factor is often the whole story on a hot path, and Big-O is the
tool that throws constants away.

**Serializing and deserializing cost.** The before lane's byte buffer is a
small, literal case of a cost that recurs at larger scale throughout real
systems: converting a value into a format it does not need, purely to
convert it back a few lines later. A decode path that already holds the
value it needs has no reason to write it to memory and read it back; doing
so pays for a round trip through RAM a register-to-register path would
skip entirely.

**Memory management.** `std::vector<std::byte>(64)` allocates on every
call; `std::vector<double>(12)` allocates again right after it. Both are
freed before the function returns, so the allocator mostly serves them
from its own free list rather than asking the operating system for new
memory. The before lane's histogram (below) still shows a tight, elevated
cluster, not a flat baseline, plus a thin scatter of slower outliers
reaching several microseconds above it, consistent with the allocator
occasionally growing its arena and touching a page for the first time.
Demand paging defers a page's real cost until something reads or writes
it, so an allocation's cost is not fixed even when its size never changes;
this chapter does not instrument page faults directly, so that stays a
plausible reading of the shape, not a measured fact. What is measured is
that the after lane, which never allocates here, has no mechanism to
produce that scatter.

**Precomputation.** The chute table and the multiplier table share a
property that makes precomputing them a clear win: a small, dense, fixed
key space (4096 decode keys, 12 chutes) whose mapping to an answer never
changes during the run. Precomputation trades a one-time setup cost, paid
once in a constructor, for a lookup cost paid on every call afterward, a
trade that pays off only when the setup amortizes over many lookups (see
"Trade-offs").

## The measurement

`examples/03-eliminating-work/cpp/` runs both lanes over the identical
5000-parcel sequence (`LineConfig::random_seed` defaults to `1` and neither
run changes it, so both simulators generate the same parcels in the same
order). Reading `examples/03-eliminating-work/results/eliminating-work.txt`:

| Lane | p50 (microseconds) | p99 (microseconds) | p99.9 (microseconds) |
|---|---|---|---|
| before | 0.596 | 0.910 | 3.619 |
| after | 0.116 | 0.189 | 0.254 |

The before lane runs about 5.1 times slower than the after lane at the
median, about 4.8 times slower at p99, and about 14.2 times slower at
p99.9. The tail widens by more than the median does: the before lane's
histogram shows a handful of parcels reaching past 4 and 5 microseconds,
well outside its own main cluster, while the after lane stays almost
entirely inside one narrow bucket. One parcel in the after lane's own
histogram lands near 6 microseconds, a single outlier in 5000 samples,
consistent with an ordinary host scheduling hiccup rather than anything
this chapter's optimization touches; it is reported here rather than
trimmed, because this guide does not quietly drop a number that does not
fit the story.

`chute_mismatches=0` in the same results file: of the 5000 parcels
compared, every one resolved to the identical chute in both lanes.
Eliminating the allocation, the map lookup, and the recomputed table
changed what it cost to reach a divert decision. It changed nothing about
which decision got reached.

## How the code works

**The manifest source.** `build_manifest_map` and `build_manifest_table`
(`include/ch03/divert_manifest.hpp`) both call `manifest_chute_for_key`
once per dense key, into a `std::unordered_map` and a
`std::array<int, key_space>` respectively. Two structures, one source,
checked directly in `tests/divert_manifest_test.cpp`
(`BuildManifestMapAndTable.AgreeWithEachOtherForEveryDenseKey`).

**The two lanes.** `ch03::BeforeScanStage::operator()`
(`src/before_scan_stage.cpp`) runs the three wasteful steps after the
decode: build and read back a four-byte record,
`manifest_map_.at(lookup_key)` to resolve the chute, and a fresh
twelve-entry `zone_multipliers` vector rebuilt from
`manifest_zone_multiplier_for_chute`. `ch03::AfterScanStage::operator()`
(`src/after_scan_stage.cpp`) replaces all three with two array indices:

```cpp
int chute = divert_table_[static_cast<std::size_t>(decode_key)];
const double zone_multiplier = zone_multipliers_[static_cast<std::size_t>(chute)];
```

Every structure either lane reads (`manifest_map_`, `divert_table_`,
`zone_multipliers_`) is built once, in its constructor; only the record,
the lookup, and the rebuild in the before lane repeat per parcel.

**The probe and the behavioral check.**
`ch03::ResolutionRecordingProbe` (`include/ch03/resolution_probe.hpp`)
records the sense-to-act span plus the chute `on_act` sees, since both
stages set `parcel.chute` before calling it. `main.cpp` runs both lanes and
compares their chute sequences position by position into
`chute_mismatches`, the same property `before_after_divert_test.cpp`
checks directly against the two stage objects at a smaller scale.

**The fragile bits.** `decode_iterations` (in `divert_manifest.cpp`) is set
low because the decode cost is identical in both lanes regardless of its
value, and keeping it small keeps the eliminated work's share of the total
large enough to see clearly. `record_size_bytes` (64) and
`key_space` (4096) are this chapter's own chosen constants, not values a
real manifest would necessarily use. Both lanes measure real wall-clock
cost on a shared, untuned scheduler, so a re-run produces numbers close to,
but not identical with, the ones quoted above; the ratios between lanes
are the stable part, not any single figure.

## Build, run, observe

```bash
cd examples/03-eliminating-work && ./demo.sh
```

Look for `before_p50_us` sitting several times above `after_p50_us`, the
same gap widening further at `before_p999_us` versus `after_p999_us`, and
`chute_mismatches=0`.

## Trade-offs

A dense precomputed table is not a universal upgrade over a map; it wins
specifically when the key space is small and bounded. This chapter's key
space is 4096 values because a decode step chose to produce one, deciding
up front that routing only needs a SKU family, not a full barcode. Resolving
the full, sparse 64-bit barcode space directly would make a dense array
impossible to hold in memory, and a hash map would be the right tool
again; the lesson is not "replace every map with an array," it is "notice
when a key space is actually small and dense, and stop paying hash-map
overhead it never needed."

The setup cost does not disappear either; it moves. Building the two
arrays in a constructor costs real time, paid once per `AfterScanStage`
instance instead of once per parcel. A program that built a fresh instance
for every single parcel would pay that cost every time and lose the entire
benefit: precomputation only pays off when the structure it builds is
reused many times after it is built.

## What you learned

- Three kinds of per-parcel waste, a heap allocation, a hash-map lookup,
  and a recomputed constant, are each individually small and collectively
  large: this chapter's measured effect is 5.1 times at the median and
  14.2 times at p99.9.
- A hash map and a dense array can share the same O(1) average-case
  complexity class and still cost very differently, because Big-O throws
  away the constant factor a cache-friendly access pattern changes.
- Precomputation trades a one-time setup cost for a per-call lookup cost,
  and only pays off when the key space is small and dense enough to hold
  as a table and gets reused enough times to amortize the setup.
- A behavioral check (`chute_mismatches=0`, backed by a property test
  across 2000 barcodes) is what turns "this looks equivalent" into
  something this chapter can actually claim.

The next chapter stays with eliminating waste, moving from whether a
lookup allocates to how a parcel's own data is laid out in memory.

## For deeper coverage

- Pekka Enberg, *Latency: Reduce Delay in Software Systems* (Manning, 2025):
  this guide's reference work for chapter arc and technique coverage; see
  `bibliography.md`.
- Thomas H. Cormen, Charles E. Leiserson, Ronald L. Rivest, and Clifford
  Stein, *Introduction to Algorithms*, 3rd edition (MIT Press, 2009): the
  average-case analysis behind a hash table's O(1) lookup, and why that
  average hides a larger constant than a direct array index.
- Jon Bentley, *Programming Pearls*, 2nd edition (Addison-Wesley, 2000):
  the classic treatment of trading space for time with a precomputed
  table, the same trade this chapter's divert table makes.
- Remzi H. Arpaci-Dusseau and Andrea C. Arpaci-Dusseau, *Operating
  Systems: Three Easy Pieces* (Arpaci-Dusseau Books, 2018; free at
  ostep.org): the virtual-memory chapters covering demand paging and why a
  page's first touch, not its allocation, is where its real cost lands.

---

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane; `ctest --preset default` passes (56
tests total across every chapter, including this chapter's new
`divert_manifest` and before/after-agreement GoogleTests; every prior
chapter's tests still pass unchanged). The ASan/UBSan lane
(`cmake --preset asan-ubsan`) builds clean, its own
`ctest --preset asan-ubsan` passes, and the example binary runs clean under
that lane with no sanitizer report.
`examples/03-eliminating-work/demo.sh` ran on an unprivileged, untuned
development host and produced `results/eliminating-work.txt` with the
before lane several times slower than the after lane at every percentile
and `chute_mismatches=0`, the effect and the behavioral guarantee this
chapter claims. Every measured number above was copied directly from that
results file. `python3 -I scripts/validate.py` passes (host-ids,
distribution, harness-cap).*
