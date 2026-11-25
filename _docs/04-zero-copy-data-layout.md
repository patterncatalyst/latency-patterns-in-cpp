---
title: "Zero-copy data layout"
order: 4
part: "Eliminating Work"
description: "The in-flight parcel ledger rebuilt from a ring of pointers to scattered heap records into one contiguous hot-field array, plus a false-sharing fix between two counters that share a cache line."
duration: 25 minutes
---

## Learning objectives

By the end of this chapter you can:

- Explain why a struct that mixes a hot field (read on every call) with cold
  fields (rarely or never read on the hot path) costs more than its size
  alone would suggest, once many of those structs sit in an array together.
- Tell the difference between array-of-structs and struct-of-arrays, and
  say which one lets a hot loop touch only the bytes it actually needs.
- Reproduce false sharing between two threads' counters, then fix it with
  `std::hardware_destructive_interference_size`, and say why the fix
  changes nothing about correctness.
- Read a profile-labelled before/after distribution for both effects and
  say, from the numbers, whether a claimed optimization held up.

## Diagram

{% include excalidraw.html file="zero-copy-layout-and-false-sharing" alt="Four panels. Top left: four pointer slots, each pointing to its own scattered heap record. Top right: one contiguous scan_keys array, with cold fields set apart below it. Bottom left: a scan thread and a divert thread both writing into one shared cache line. Bottom right: the same two threads, each writing into its own separate cache line." caption="Figure 4.1: Two unrelated layout changes, read the same way: what sits next to what, and what that costs the cache." %}

## Why this matters

The previous chapter removed a heap allocation, a hash-map lookup, and a
recomputed table from the scan decode path, several times faster doing
provably the same work. None of those three fixes touched where a
parcel's own data lives in memory. This chapter does.

A real sortation line checks every new parcel's barcode against the
parcels scanned just before it, to catch a double feed or a re-scanned
item before it reaches the belt a second time under a different chute
assignment. That check looks at a window of recent records, so how those
records sit in memory decides how many cache lines the lookup touches.
Put a parcel's rarely-read fields between its hot ones, store each record
behind its own pointer, and a loop that needs four bytes per record pays
for an entire scattered cache line instead. Pack only the hot bytes into
one array, and the same loop holds the whole working set in a few
kilobytes of cache.

The second half of this chapter is a different neighbor problem. Two
threads, one standing in for the scan stage and one for the divert stage,
each keep a running count of parcels handled. If those two counters land
on the same 64-byte cache line, every increment from one thread
invalidates the other's cached copy of that line, even though neither
thread ever touches the other's variable. The fix costs nothing but
space.

The code is in `examples/04-zero-copy-layout/`; its `README.md` covers
what it does and how to drive it.

## The before layout: a ring of pointers to scattered records

`ch04::ParcelLedgerBefore` keeps a fixed-capacity ring of the last 256
scanned parcels, so the scan stage can check a new parcel's `scan_key` (a
barcode folded into a dense 701-value space, the hash shape the previous
chapter uses to a different end) against everything still in the window. The ring
itself, `std::vector<std::unique_ptr<ParcelRecordBefore>>`, is contiguous.
What it holds is not: every slot owns an independent heap allocation, the
shape a plain array of objects takes in many languages by default.
Checking 256 scan keys means following 256 unrelated pointers, each one
likely landing on a cache line the previous one did nothing to warm.

`ParcelRecordBefore` itself does the rest of the damage:

```cpp
struct ParcelRecordBefore {
    std::uint64_t barcode = 0;              // hot: identity
    std::array<char, 64> origin_label{};    // cold
    int scan_key = 0;                       // hot: what the duplicate check reads
    std::array<char, 64> scan_notes{};      // cold
    double weight_kg = 0.0;                 // not read by the duplicate check
    std::array<char, 64> customer_ref{};    // cold
    int chute = -1;                         // hot: the result
};
```

Three cold fields, an origin label, a free-text handling note, and a
customer reference, sit between the two hot fields the duplicate check
reads. None is a pointer or a second heap buffer; the previous chapter
already measured what a per-parcel allocation costs. The struct is wide (over 200
bytes) for a reason that has nothing to do with allocation: `scan_key`
shares a cache line with 60-odd bytes the hot loop never touches on this
pass.

The duplicate check makes one more thing worse on top of the layout:

```cpp
ParcelRecordBefore record = *record_ptr;   // copy the whole record out of
if (record.scan_key == scan_key) {          // its heap slot before reading
    return true;                            // the one field this needs
}
```

Reading `record_ptr->scan_key` directly would need no copy. Copying the
full record out first, the same "convert to use, then throw away" shape
the previous chapter eliminated for a single value, here runs once per
record in the window, every parcel.

## The after layout: hot fields packed, cold fields set apart

`ch04::ParcelLedgerAfter` answers the identical question, duplicate or not,
from a different shape. Its hot column is one contiguous array:

```cpp
std::vector<int> scan_keys_;    // hot, contiguous: the only field the
                                 // duplicate check reads
std::vector<ColdRecord> cold_;  // cold, contiguous but never read here
```

The duplicate check borrows a view over the live slice instead of copying
anything:

```cpp
const std::span<const int> keys(scan_keys_.data(), count_);
return std::ranges::any_of(keys, [scan_key](int key) { return key == scan_key; });
```

`std::span` makes the borrow explicit in the type: `contains_scan_key`
takes no ownership of the data it reads and allocates nothing to read it.
256 four-byte integers fit in one kilobyte, a few cache lines total
regardless of where the ring wraps, instead of 256 independent heap
addresses. The cold fields (`ColdRecord`, the same three blobs the before
lane carries) move into their own contiguous `std::vector`, parallel to
`scan_keys_` by index: never allocated, never touched, never sharing a
line with the bytes the hot loop reads. Nothing about the parcel's
information was dropped to go faster; it was separated by how often it
gets read.

Both ledgers resolve the identical decision from the identical input: a
match routes to a dedicated `review_chute`, a miss routes to
`scan_key % divert_chute_count`. `tests/layout_scan_stage_test.cpp` checks
this across 2000 barcodes and an engineered repeat, the proof pattern the
previous chapter uses for its own before/after pair.

## False sharing: two counters, one cache line

The ledger lanes above are the layout half of this chapter. The second
half is a sharper case of the same idea: two independent values, each
owned by a different thread, interacting anyway because of where they
sit.

```cpp
struct UnpaddedCounters {
    std::atomic<std::uint64_t> scan_count{0};
    std::atomic<std::uint64_t> divert_count{0};
};
```

Sixteen bytes, well inside one 64-byte line. The two threads never read or
write each other's variable, but the cache-coherency protocol tracks
ownership at the granularity of a line, not a field: every write from one
thread invalidates the other core's cached copy of the whole line,
forcing a fresh fetch before its own next increment completes. The fix
adds nothing to what either thread does, only to where each counter
lives:

```cpp
struct PaddedCounters {
    struct alignas(std::hardware_destructive_interference_size) Slot {
        std::atomic<std::uint64_t> value{0};
    };
    Slot scan_slot;
    Slot divert_slot;
};
```

`std::hardware_destructive_interference_size` is the standard library's
own name for the line size this fix needs to respect, 64 bytes on this
host. Aligning each `Slot` to it guarantees `scan_slot` and `divert_slot`
can never land in the same line, however the allocator places the
enclosing object. Two `static_assert`s in `false_sharing.hpp` check this
at compile time, one per type, so a build fails before a single trial
runs if either layout stops matching its name.

One layout detail mattered while measuring this: `run_trial`
heap-allocates its `Counters` instance with `std::make_unique` rather than
a stack local. `::operator new` guarantees at least 16-byte alignment, and
16 divides 64 evenly, so a 16-byte `UnpaddedCounters` can never straddle a
line boundary on the heap; a stack local only inherits `alignof` (8), and
one offset in eight would have split the counters across a line by
accident, erasing the effect this lane exists to show.

## Memory topology and cache locality

Both effects above are instances of one rule: distance in memory costs
time to cross, and the cost is not linear. L1 costs a few cycles; L2,
several times that; L3, further still; another socket's memory over the
interconnect, further again. The ledger lanes measure the
L1-versus-further-out part of that ladder, by changing how many distinct
lines a fixed-size scan touches. The false-sharing lane measures a
different rung: the two cores are not reading slow memory at all, they
are paying the coherency protocol's cost to keep a line both of them
think they own exclusively.

NUMA locality sits one rung further out: a read from a remote socket's
memory costs more than one from the local socket's, regardless of cache
level. This host cannot show that number. `lscpu` reports `NUMA node(s):
1`, a single-socket laptop CPU with one memory controller; there is no
remote node to be slow. The effect is real on multi-socket hardware and
absent, not just small, here: this chapter ships no NUMA lane, rather
than a number too noisy to trust.

## The measurement

`examples/04-zero-copy-layout/cpp/` runs the ledger lanes over the
identical 5000-parcel sequence (`LineConfig::random_seed` defaults to `1`)
and the false-sharing lanes as 30 trials each of 10,000,000 increments per
thread. From `results/zero-copy-layout.txt`, measured on a
16-core/22-thread laptop CPU, powersave governor, bare metal, no pinning
or isolation beyond the two worker threads' own affinity:

| Lane | p50 | p99 | p99.9 |
|---|---|---|---|
| ledger before (AoS, pointer-chasing) | 7.315 us | 12.626 us | 17.628 us |
| ledger after (SoA, contiguous) | 3.921 us | 4.890 us | 8.450 us |
| counters unpadded (shared line) | 226.580 ms | 248.378 ms | 248.378 ms |
| counters padded (separate lines) | 60.631 ms | 62.788 ms | 62.788 ms |

The ledger's before lane runs about 1.9 times slower than the after lane
at the median and about 2.6 times slower at p99. The unpadded counters run
about 3.7 times slower than the padded counters at the median and about
4.0 times slower at p99.9. Both effects showed up clearly and repeatably
on this untuned host; the false-sharing ratio varied between repeated runs
while tuning the trial size (3.1 to 4.3 times), consistent with a
coherency cost sensitive to how the scheduler interleaves two busy
threads, not with the effect disappearing.

`before_review_count` and `after_review_count` both read `1525` of 5000
parcels, and `chute_mismatches=0`: the two ledger layouts flag the
identical parcels for manual review and route every non-flagged parcel to
the identical chute. `unpadded_count_mismatches` and
`padded_count_mismatches` both read `0` across all 30 trials each: every
trial's two counters land on exactly 10,000,000, regardless of which
layout backs them. Changing where two numbers live changed how long they
took to update, never what they added up to.

## How the code works

**The scan key and the two stages.** `ch04::scan_key_for_barcode` folds a
barcode into the 701-value dense space both ledgers key on, identical in
both lanes. `BeforeLayoutScanStage` and `AfterLayoutScanStage`
(`src/layout_scan_stage.cpp`) each compute the key, ask their ledger
whether it has been seen in the current window, and push the new record.
Only the ledger type passed into each stage's constructor differs.

**The ledgers.** `ParcelLedgerBefore::contains_scan_key`
(`src/parcel_ledger_before.cpp`) walks a ring of `unique_ptr` and copies
each record out before comparing. `ParcelLedgerAfter::contains_scan_key`
(`src/parcel_ledger_after.cpp`) borrows a `std::span` over a contiguous
`std::vector<int>` and calls `std::ranges::any_of`. Both read the same
total information; only one of them copies anything to do it.

**The counters and the trial.** `run_unpadded_trial` and
`run_padded_trial` (`src/false_sharing.cpp`) both call a shared, private
`run_trial<Counters>` template, which spawns one thread per counter,
best-effort pins each to a distinct physical core by reading
`/sys/devices/system/cpu/cpu*/topology/core_id` (unreadable topology just
means the trial runs unpinned rather than guessing a host-specific CPU
number), and times both threads to completion.

## The Rust ownership sidebar

Rust's borrow checker makes the choice this chapter's C++ code makes by
convention, between borrowing a view and copying a value, a compile-time
rule instead of a reviewer's judgment call. A function taking `&[i32]`
cannot keep that slice past its owner's lifetime and cannot mutate
through it without also declaring `&mut`; a function that wants to keep
data around clones it explicitly, with `.to_vec()`, visibly, at the call
site. The `std::span` in `ParcelLedgerAfter` borrows the same way, by
convention rather than compiler enforcement; C++ trusts the caller not to
outlive the vector it points into.

None of that changes the number this chapter measured. The gap between
the two ledger layouts comes from where 256 records' hot fields sit
relative to each other, not from who owns them; a Rust program with the
same array-of-boxed-structs shape would pay the same scattered-cache-line
cost `Vec<Box<Record>>` carries, regardless of how clean its ownership
rules read. The false-sharing fix is just as language-neutral: Rust's
`crossbeam-utils` crate ships a `CachePadded<T>` wrapper doing what this
chapter's `PaddedCounters::Slot` does, because the cache line consults
neither language's type system before deciding what counts as one owner's
data. Layout decided this chapter's numbers, which is why Rust stays a
sidebar rather than a second measured lane.

## Build, run, observe

```bash
cd examples/04-zero-copy-layout && ./demo.sh
```

Look for `layout_before_p50_us` sitting roughly twice `layout_after_p50_us`,
`unpadded_p50_us` sitting roughly four times `padded_p50_us`, and both
`chute_mismatches=0` and the two `*_count_mismatches` fields reading `0`.

## Trade-offs

Separating hot and cold fields only pays off when a loop revisits many
records' hot fields without needing their cold ones on the same pass. A
path that reads every field of every record gains nothing from
struct-of-arrays; it would just turn one sequential read into several.
This chapter's duplicate-scan loop, one field read 256 times, is exactly
the shape that wins.

Padding a counter out to a full cache line costs real memory: 64 bytes for
an 8-byte value, 56 spent on nothing. Worth it for two values two threads
update constantly; not worth it for a field one thread owns, or a large
array of small values no thread contends over, where the same padding
would bloat the array past its working set for no benefit. False sharing
is a threading cost, not a layout cost in general, and the fix should stay
scoped to where threads actually collide.

## What you learned

- A struct that interleaves hot and cold fields costs a hot loop more
  cache lines per useful byte than the same fields split apart, even with
  no additional allocation anywhere in the picture.
- Array-of-structs-of-pointers (a ring of `unique_ptr`) and
  struct-of-arrays answer the identical question from the identical
  input; `std::span` makes "read this without copying it" a type, not a
  convention to remember.
- Two independent values, each owned by a different thread, can still
  slow each other down if they share a cache line, because the
  cache-coherency protocol tracks ownership by line, not by field; the
  fix, `std::hardware_destructive_interference_size`, is cheap and
  changes nothing about correctness.
- NUMA locality is real and this chapter could not show it: a
  single-socket host has no remote node to be slow against.

The next chapter stays with two threads on the hot path, moving from
where their data sits to how they hand work to each other without a lock.

## For deeper coverage

- Pekka Enberg, *Latency: Reduce Delay in Software Systems* (Manning,
  2025): this guide's reference work for chapter arc and technique
  coverage; see `bibliography.md`.
- Ulrich Drepper, "What Every Programmer Should Know About Memory" (Red
  Hat, 2007): the cache-line and false-sharing mechanics this chapter
  measures directly, from the engineer who wrote the canonical paper on
  it.
- Herb Sutter, "Machine Architecture: Things Your Programming Language
  Never Told You" (talk, 2007): array-of-structs versus struct-of-arrays,
  and why the gap between them grows with the width of the struct.

---

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane; `ctest --preset default` passes (62
tests total, including this chapter's new `layout_scan_stage` and
`false_sharing` GoogleTests; every prior chapter's tests still pass
unchanged). The ASan/UBSan lane (`cmake --preset asan-ubsan`) builds
clean, its own `ctest --preset asan-ubsan` passes, and the example binary
runs clean under that lane with no sanitizer report. Because this
chapter's false-sharing lane is multithreaded, the TSan lane (`cmake
--preset tsan`) was also built and run directly against the example
binary, with no data-race report.
`examples/04-zero-copy-layout/demo.sh` ran on an unprivileged, untuned
development host and produced `results/zero-copy-layout.txt` with the
ledger's before lane consistently slower than the after lane at every
percentile, the unpadded counters consistently slower than the padded
counters, and both `chute_mismatches=0` and the two `*_count_mismatches`
fields at `0`. Every number above was copied directly from that results
file. `python3 -I scripts/validate.py` passes (host-ids, distribution,
harness-cap).*
