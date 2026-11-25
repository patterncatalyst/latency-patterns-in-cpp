---
title: "Latency as a distribution"
order: 1
part: "Measuring Latency"
description: "Why a mean misrepresents scan-to-divert under load, and why coordinated omission is physical on this belt, not a definition to memorize."
duration: 25 minutes
---

## Learning objectives

By the end of this chapter you can:

- Explain why a single average collapses the shape a tail-focused budget
  check needs to see, using this chapter's own measured numbers as evidence.
- Read a naive scan-to-divert distribution against the same run's
  coordinated-omission-corrected distribution and say which parcels the
  naive measurement missed, and why.
- Install a periodic stall and a real per-parcel decode cost into the scan
  stage with `set_stage_work`, without touching the line-simulator itself.
- Run `examples/01-latency-as-a-distribution/demo.sh` and connect its two
  distributions back to this chapter's measured numbers.

## Diagram

{% include excalidraw.html file="mean-versus-tail" alt="A schematic scan-to-divert distribution showing a fast-path cluster and a separated stall tail, with the naive mean, the naive p99, and the corrected p99 each marked at the point they actually land." caption="Figure 1.1: The naive mean sits in the empty space between the two clusters; the naive p99 still sits inside the fast path; the corrected p99 lands inside the stall tail." %}

## Why this matters

The running example's measurement harness, the line-simulator, and the
scan-to-divert metric now all exist (see the running example and harness
chapter), and all of them ran once against an idle line. The scan stage did
almost no work there: a probe callback and a modulo lookup, nothing else,
so its distribution was a narrow bump just above the noise floor. That run
proved the harness works. It said nothing about what a real scan-to-divert
distribution looks like, because nothing on that line was slow.

This chapter puts the first real cost into the scan stage: a per-parcel
decode plus a periodic stall, and asks what the resulting distribution
means for the question most people actually ask when they report a number
like this: how slow is too slow? A mean promises a single answer. On this
chapter's own measured run, that single number does not resemble what any
individual parcel experienced: the naive arithmetic mean sits about 31
times above the fast-path median, and about 240 times below the slowest
parcels the same run recorded. That gap is not a modeling error. It is the
shape of this distribution, and reading that shape, rather than averaging
it away, is this chapter's subject.

The code is in `examples/01-latency-as-a-distribution/`. The run script
there builds, sets up, and runs it; its `README.md` covers what it does and
how to drive it.

## Why a distribution, not a mean

`measure::LatencyRecorder` has never had a `mean()` method (see the running
example and harness chapter), and this chapter is why. A mean divides a
total by a count, and in doing so it treats every sample as interchangeable
with every other: a parcel that cleared the scanner in a microsecond counts
exactly as much toward the answer as a parcel that sat behind a stall for
eight milliseconds. For a budget check, that is exactly backward. The
parcels at risk of missing the scan-to-divert deadline are the slow ones,
never the typical ones, and a mean actively buries them: enough fast
samples will always outnumber a handful of slow ones, so the average keeps
drifting toward "mostly fine" no matter how severe the rare failures get.

A percentile asks a different question: what value does the Nth parcel out
of every hundred (or every thousand) sit at or below? `p50` answers "what
did a typical parcel see." `p99` and `p99.9` answer "what did the worst 1%,
or the worst 0.1%, see," which is the question a deadline actually cares
about. A histogram or an eCDF goes one step further and shows the whole
shape at once: how many parcels cluster near the fast path, how far
separated the slow ones are, and whether there is one tail or several.
None of those three throws away the samples that matter to average the
ones that do not.

This chapter's own numbers make the point concrete. Reading
`examples/01-latency-as-a-distribution/results/loaded-distribution.txt`:

```model
# the naive arithmetic mean, shown here next to the percentiles computed
# from the SAME 5000 raw samples, to demonstrate why it misleads. It is
# not a reported scan-to-divert result anywhere else in this guide.
naive_mean_us = 33.373
naive_p50_us  = 1.085
naive_p99_us  = 3.204
naive_p999_us = 8060.819
```

Every one of those four figures came out of the identical 5000-sample run.
The mean sits well above the median, which says a handful of samples pulled
it upward, and well below `p99.9`, which says those samples were far more
severe than the mean itself suggests. A reader who only saw the mean would
conclude the line runs at roughly 33 microseconds and call it a day; a
reader who saw all four numbers would ask what happens to the one parcel in
a few hundred that misses the fast path entirely, and that second question
is the one a sortation line's operators actually need answered.

## Coordinated omission is physical on this belt

A percentile only tells the truth about the samples it was given. If the
measurement itself systematically fails to capture the parcels most harmed
by a slow stage, no amount of percentile math recovers them. This is the
problem Gil Tene named coordinated omission: a generator that waits for one
operation to finish before starting the next "coordinates" with whatever
made that operation slow, so the delay never gets attributed to the
arrivals queued up behind it. CLAUDE.md's domain note has said since the
scaffold: the belt never waits for a slow consumer. That line was an
abstract warning before this chapter ran code. Now it is a measured fact
about a parcel sortation line: each `Parcel`'s `scheduled_arrival` is fixed
the moment it is created from the configured arrival rate (see
`line_simulator/parcel.hpp`), and nothing in the simulator ever pushes that
schedule back to wait for a stage that is running long. A real belt behaves
the same way. The scanner's one stall does not pause the parcels still
approaching it on the belt; they keep coming at their own pace, and every
one of them that arrives while the scanner is still stalled inherits part
of that delay, whether or not a probe happens to be watching closely enough
to record it.

A probe that only records the one span it measured sees a single slow
sample and nothing else. `measure::LatencyRecorder::record()` corrects for
this when it is given an expected inter-arrival interval: a sample that ran
longer than that interval implies that every arrival scheduled behind it
waited a proportionally longer time too, so the recorder backfills those
missing samples instead of silently dropping them. This chapter's run turns
that correction on for the first time, and the mechanism needs nothing
instrumented about the schedule to work: it already has the span, and it
already knows what interval parcels were supposed to arrive at.

## The loaded scenario

Everything this chapter measures comes from one run of 5000 parcels past
the same five stages the running example and harness chapter established,
at a nominal rate of 2000 parcels per second (an arrival every 500
microseconds). Two things load the
scan stage that an idle run never exercised:

- **A decode cost on every parcel.** Before a real scanner can divert a
  parcel, it decodes a barcode and typically cross-checks it against the
  shipment's declared dimensions. This chapter's scan stage runs a short
  checksum loop over the parcel's barcode and compares its dimensional
  weight (length times width times height, divided by 5000, the courier
  industry's standard volumetric-weight formula) against its actual weight,
  to decide whether it needs an oversize chute. Neither computation is
  free, and this is the first chapter where that cost shows up in the
  measurement at all.
- **A stall on one parcel in 250.** Every 250th parcel pays an extra 8
  millisecond delay, standing in for a cause a real line would eventually
  have to diagnose: a garbage-collection-style pause, a lock a downstream
  consumer is holding, or (concretely, on this line) the label printer's
  spooler blocking the scan stage while it flushes a full buffer. Moving
  work like that off the hot path is a later chapter's technique; this
  chapter only needs the stall to exist so there is something real for a
  distribution, and a correction, to find.

At a 0.4% stall rate (one parcel in 250), the slow population is smaller
than the top 1% a `p99` cutoff reaches but larger than the top 0.1% a
`p99.9` cutoff reaches. That arithmetic comes directly from this chapter's
chosen parameters: the measured run shows a percentile missing a real,
repeating failure and a different percentile catching it, side by side, on
the same data.

## How the code works

**The workload.** `ch01::LoadedScanStage` (`include/ch01/loaded_scan_stage.hpp`)
is a callable installed via `LineSimulator::set_stage_work(Stage::Scan, ...)`.
Overriding a stage skips the simulator's own default work for that stage,
including its probe calls, so this type has to drive sense, decide, and act
itself, in the same order the running example and harness chapter's adapter
expects them:

```cpp
void operator()(Parcel& parcel, Stage stage) const {
    if (stage != Stage::Scan) {
        return;
    }
    probe_->on_sense(parcel, Clock::now());

    std::uint64_t checksum = parcel.barcode;
    for (int i = 0; i < decode_iterations; ++i) {
        checksum = (checksum * hash_multiplier) ^ (checksum >> hash_shift);
    }
    const double volumetric_weight_kg =
        (parcel.length_cm * parcel.width_cm * parcel.height_cm) / volumetric_divisor_cm3_per_kg;
    const bool oversize_for_weight = volumetric_weight_kg > parcel.weight_kg * oversize_margin;
    parcel.chute = oversize_for_weight
                       ? oversize_chute
                       : static_cast<int>(checksum % static_cast<std::uint64_t>(chute_count));

    if (stall_.every_n_parcels > 0 && parcel.id % stall_.every_n_parcels == 0) {
        std::this_thread::sleep_for(stall_.duration);
    }
    probe_->on_decide(parcel, Clock::now());
    probe_->on_act(parcel, Clock::now());
}
```

The early return for every stage but `Scan` matters: `LineSimulator` calls
this same callable for all five stages once it is installed as the scan
stage's work, so the function has to recognize which stage it is being
asked about and do nothing for the other four. The checksum loop and the
volumetric-weight check both read and write only `parcel`, which is why
`LoadedScanStage` can be tested by constructing a `Parcel` directly and
calling the operator, without running the simulator at all (see
`tests/loaded_scan_stage_test.cpp`). The stall check comes after the chute
is assigned and before `on_decide`, so the recorded span includes it: a
later chapter that moves work like this off the hot path will be measured
against the span this chapter establishes as the baseline that includes it.

**The probe.** `ch01::SampleCollectingProbe` (`include/ch01/sample_collecting_probe.hpp`)
plays the same role the running example and harness chapter's
`RecordingProbe` did, with one difference:
instead of recording straight into a `LatencyRecorder`, it appends each
span to a plain `std::vector<Sample>`. That choice is what makes the
naive-versus-corrected comparison possible from a single run: the raw
sequence gets captured exactly once, and gets replayed afterward into two
different recorders, rather than running the whole scenario twice and
hoping two separate runs agree closely enough to compare.

**The naive/corrected split.** `ch01::build_naive_and_corrected`
(`include/ch01/distribution_stats.hpp`) takes that one raw sequence and an
expected inter-arrival interval, and returns two `LatencyRecorder`
instances: one constructed with no expected interval (so `record()` never
backfills), and one constructed with the belt's own interval (so it does.)
Replaying the identical sequence into both is a controlled before/after:
any difference between the two distributions comes from the correction
alone, not from any difference in what actually happened on the line.

```cpp
NaiveAndCorrected build_naive_and_corrected(const std::vector<Sample>& samples,
                                             Sample expected_interval) {
    NaiveAndCorrected result{
        .naive = measure::LatencyRecorder{},
        .corrected = measure::LatencyRecorder{expected_interval},
    };
    for (const Sample& sample : samples) {
        result.naive.record(sample);
        result.corrected.record(sample);
    }
    return result;
}
```

**The mean.** `ch01::compute_mean`, in the same header, is a five-line free
function kept outside `measure::LatencyRecorder` (see "Why a distribution,
not a mean" above): it exists only so this chapter's example
can report the one number it is critiquing, next to the percentiles
computed from the exact same samples.

**The entry point.** `src/main.cpp` wires all of the above together: build
a `LineConfig` with `parcel_count = 5000` and `parcels_per_second = 2000.0`,
construct the `LoadedScanStage` with a `StallSchedule{.every_n_parcels = 250,
.duration = 8ms}`, install it as the scan stage's work, run the line, then
compute the mean and both recorders from `probe->samples()` and write
everything to `results/loaded-distribution.txt`. The expected interval
passed to `build_naive_and_corrected` is `1.0 / config.parcels_per_second`,
the same 500 microsecond figure the stall schedule is sized against.

**The fragile bits.** The stall uses `std::this_thread::sleep_for`, an
application-level delay inside this process, never a host scheduling
change (see CLAUDE.md, "Never change the development host"): it costs real
wall-clock time on whatever core happens to run it, which is exactly what a
real GC pause or lock contention would cost too, but it also means this
chapter's exact numbers carry some run-to-run jitter from the host's own
scheduler. The 250-parcel stall period and the 2000-parcel-per-second
nominal rate are this chapter's own chosen parameters, picked so the
measured effect lands where the text above describes it, not values a real
sortation line would necessarily use.

## Build, run, observe

```bash
cd examples/01-latency-as-a-distribution && ./demo.sh
```

Look for `naive_mean_us` sitting well above `naive_p50_us` in the printed
output and in `results/loaded-distribution.txt`, for `naive_p99_us` staying
close to `naive_p50_us` while `naive_p999_us` jumps by three orders of
magnitude, and for `corrected_p99_us` landing far above `naive_p99_us` even
though both came from the same 5000 raw samples.

## Cross-check

`corrected_sample_count` in the results file should be larger than
`naive_sample_count`, because every stall backfills extra samples; it
should never be smaller, since `LatencyRecorder::record()` only ever adds
samples, never removes the original one. On this chapter's measured run,
`raw_sample_count` and `naive_sample_count` are both 5000 (one sample per
parcel, no backfill, the same shape the running example and harness
chapter's own uncorrected baseline reported), while `corrected_sample_count`
is 5320: 20 stalls
(5000 parcels divided by 250), each backfilling roughly 15 extra samples
because an 8 millisecond stall against a 500 microsecond expected interval
leaves about 15 whole intervals of missed arrivals behind it. That
arithmetic is a hand check anyone can run against the same config values
the results file already prints, the same way the running example and
harness chapter's deadline cross-check worked.

## What you learned

- A mean divides a total by a count and, in doing so, treats a rare severe
  delay as interchangeable with a typical fast sample; a percentile and a
  histogram do not.
- Coordinated omission is a measurement failure, not a property of the
  system being measured: a probe that only records one span per operation
  misses every arrival that queued up behind a slow one, and
  `LatencyRecorder::record()`'s backfill exists to recover them.
- A stall rate below a percentile's cutoff can make that percentile miss a
  real, repeating failure outright, while a stricter percentile at the same
  run catches it; picking which percentile to trust is part of reading a
  distribution, not a detail to skip past.
- `LineSimulator::set_stage_work` is enough to install a real workload, a
  stall, and a probe adapter into the shared line without touching the
  simulator itself, the same seam a later chapter's technique installs
  into.

The next chapter keeps this same loaded intuition and turns it into a
number that can be computed before a single parcel runs: the latency
budget scan-to-divert's deadline implies, and what Little's Law says about
how many parcels should be in flight against it.

## For deeper coverage

- Pekka Enberg, *Latency: Reduce Delay in Software Systems* (Manning, 2025):
  this guide's reference work for chapter arc and technique coverage; see
  `bibliography.md`.
- Gil Tene, "How NOT to Measure Latency" (conference talk, Azul Systems):
  the source for the coordinated-omission correction
  `measure::LatencyRecorder` implements and this chapter turns on for the
  first time.
- Gil Tene's HdrHistogram project (hdrhistogram.org) implements the same
  backfill idea in production-grade tooling; this guide's
  `LatencyRecorder` is a minimal, from-scratch version sized to be read end
  to end in one sitting, not a replacement for it.

---

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane; `ctest --preset default` passes,
including the new `LoadedScanStage` and `distribution_stats` GoogleTests.
The ASan/UBSan lane (`cmake --preset asan-ubsan`) builds clean, its own
`ctest --preset asan-ubsan` passes, and the example binary itself was run
under that lane with no sanitizer report.
`examples/01-latency-as-a-distribution/demo.sh` was run on an unprivileged,
untuned development host and produced
`results/loaded-distribution.txt` with a naive mean far above the naive
median, a naive p99 that misses the stall population outright, and a
corrected p99 that moves into that population, which is the effect this
chapter claims; the naive p99.9, which already caught the stall without
correction, barely moves, and the text above says so rather than
overstating the correction's reach. Every number quoted in this chapter was
copied directly from that results file. `python3 -I scripts/validate.py`
passes (host-ids, distribution, harness-cap).*
