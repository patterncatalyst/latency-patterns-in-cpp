---
title: "The running example and the harness"
order: 0
part: "Measuring Latency"
description: "The parcel sortation line, the scan-to-divert metric, and the measurement harness every later chapter reuses."
duration: 20 minutes
---

## Learning objectives

By the end of this chapter you can:

- Explain what `scan-to-divert` measures on the parcel sortation line and why
  its deadline comes from the belt's own geometry.
- Read a `LatencyRecorder`'s `percentile()`, `histogram()`, and `ecdf()`
  output and say why none of them is a mean.
- Wire a `line_simulator::ScanToDivertProbe` into the shared line-simulator to
  record a measurement without touching the simulator itself.
- Run `examples/00-running-example/demo.sh` and read the resulting
  profile-labelled `results/baseline.txt`.

## Diagram

{% include excalidraw.html file="scan-to-divert-line" alt="A parcel sortation line with five stages: checkweigh, dimension, scan, label, sort. A bracket under scan and sort marks the scan-to-divert measurement window." caption="Figure 0.1: The parcel moves through five stages; scan-to-divert is the sense-to-act span this guide measures in every chapter." %}

## Why this matters

Every chapter in this guide applies one latency technique to the same
example and checks the same number afterward. Before any of that can mean
anything, the example and the number need to exist, and the number needs a
budget to be judged against. This chapter builds both: the parcel
sortation line that every later chapter drives, and the scan-to-divert
measurement that every later chapter reports. No technique appears here. If
a later chapter's lane looks faster, this chapter's run is the baseline it
got faster than.

The code is in `examples/00-running-example/`. The run script there builds,
sets up, and runs it; its `README.md` covers what it does and how to drive
it.

## The running example

A parcel moves down a belt through five stages, in order: checkweigh,
dimension, scan, label, sort. `examples/common/line-simulator/` is the
shared implementation every chapter drives, with `Stage` as a five-value
enum and `stage_order` fixing the sequence:

```cpp
enum class Stage : std::uint8_t {
    Checkweigh = 0,
    Dimension = 1,
    Scan = 2,
    Label = 3,
    Sort = 4,
};
```

A chapter that wants to change what happens at one stage calls
`set_stage_work(stage, work)` and leaves the other four alone. A caching
lane, for instance, only touches the stage that resolves a route; checkweigh
and dimension never change. This is why the guide stays at one running
example instead of one example per chapter: the belt, the parcel record, and
the stage sequence are fixed, and a technique swaps one stage's work inside
the same program.

The belt never waits for a slow stage. Each `Parcel`'s `scheduled_arrival` is
computed once, from the configured arrival rate, when the parcel is created,
and nothing in the simulator ever pushes that timestamp back to match how
long a stage took:

```cpp
struct Parcel {
    std::uint64_t id = 0;
    std::uint64_t barcode = 0;
    // ... weight, dimensions ...
    int chute = -1;
    Clock::time_point scheduled_arrival;
};
```

A real sortation line works the same way: parcels keep arriving at their own
rate no matter how busy the scanner is. A benchmark that measures one
operation at a time and waits for it to finish before starting the next one
does not have this property, and the gap between the two is a known failure
mode in latency measurement called coordinated omission. This chapter's
harness does not yet turn on the correction for it (the next chapter does);
what matters here is that the simulator's arrival schedule is already
real, so the correction has something physical to correct against once it is
turned on.

## The scan-to-divert metric and its budget

Scan-to-divert is sense, decide, act: the scanner reads a barcode (sense),
the routing lookup resolves a chute (decide), and the diverter arm fires
(act). `ScanToDivertProbe` is the seam the line-simulator calls into for all
three, and it never interprets the timestamps itself:

```cpp
class ScanToDivertProbe {
public:
    virtual void on_sense(const Parcel& parcel, Clock::time_point at) = 0;
    virtual void on_decide(const Parcel& parcel, Clock::time_point at) = 0;
    virtual void on_act(const Parcel& parcel, Clock::time_point at) = 0;
};
```

The deadline for this span is not a target someone picked. It falls out of
the belt's geometry: a parcel travels at `belt_speed_m_per_s`, the diverter
sits `scan_to_diverter_distance_m` past the scanner, so the line physically
cannot route a parcel after it reaches the diverter.
`LineSimulator::scan_to_divert_deadline()` is exactly that division:

```cpp
std::chrono::duration<double> LineSimulator::scan_to_divert_deadline() const {
    return std::chrono::duration<double>(config_.scan_to_diverter_distance_m /
                                          config_.belt_speed_m_per_s);
}
```

Every chapter from here on measures scan-to-divert against this same
formula. Miss the deadline and the parcel passes the diverter unrouted; no
one negotiated this budget, the belt's geometry sets it.

## How the harness measures

`measure::LatencyRecorder` is the shared measurement harness
(`examples/_lib/`). It reports a distribution: `percentile(p)` for any `p` in
`(0, 100]`, a `histogram()`, and an `ecdf()`. There is no `mean()` method and
there will not be one added to this type. A single average collapses a
shape that has a point: most parcels clear the scanner quickly and a few
sit in a long tail, and the tail is exactly what a budget check needs to
see. Reporting one number instead of the shape would hide the parcels that
are closest to missing the deadline, which are the only ones a latency
chapter cares about.

`record()` also does Gil Tene's coordinated-omission correction, given an
expected inter-arrival interval:

```cpp
void LatencyRecorder::record(std::chrono::duration<double> latency) {
    samples_.push_back(latency.count());
    const double expected = expected_interval_.count();
    if (expected <= 0.0) {
        return;  // no expected-interval configured; nothing to correct
    }
    double missed = latency.count() - expected;
    while (missed > 0.0) {
        samples_.push_back(missed);
        missed -= expected;
    }
}
```

If one operation runs long while new arrivals keep showing up on schedule,
every arrival that piled up behind it waited proportionally longer too, and
this loop backfills those missing samples instead of reporting only the one
sample the generator happened to observe. This chapter constructs its
`LatencyRecorder` with no expected interval, so the correction stays off here
and the baseline below is the uncorrected number; the next chapter turns the
correction on and shows what changes.

## How the code works

`examples/00-running-example/cpp/` has three pieces: a header that adapts
the simulator's probe interface to the recorder, the program that runs the
line and writes the results, and the tests for that adapter.

**The adapter.** `RecordingProbe` is the one new type this chapter adds. It
implements `ScanToDivertProbe` and holds a reference to the
`LatencyRecorder` it feeds:

```cpp
class RecordingProbe : public line_simulator::ScanToDivertProbe {
public:
    explicit RecordingProbe(measure::LatencyRecorder& recorder) : recorder_(recorder) {}

    void on_sense(const Parcel&, Clock::time_point at) override { sense_at_ = at; }
    void on_decide(const Parcel&, Clock::time_point /*at*/) override {}
    void on_act(const Parcel&, Clock::time_point at) override {
        recorder_.record(at - sense_at_);
    }

private:
    measure::LatencyRecorder& recorder_;
    Clock::time_point sense_at_;
};
```

A single `Clock::time_point` member is enough here: the line-simulator runs
one parcel's checkweigh, dimension, scan, label, and sort calls to
completion before it starts the next parcel's (see `LineSimulator::run()`),
so `on_sense` for parcel N always arrives, and is consumed by `on_act`,
before `on_sense` for parcel N+1. A map keyed by parcel id would solve a
problem this harness does not have yet; fanning the sort decision across
concurrent diverter lanes is where interleaved parcels would force that
change. `on_decide` receives the callback and records nothing: the chapter's
spine metric is the full sense-to-act span, and an empty override, rather
than omitting the method, keeps `RecordingProbe` a complete, mockable-shaped
implementation of the interface.

**The entry point.** `main.cpp` wires the pieces together: build a
`LineConfig` at its defaults, construct the `LineSimulator`, construct a
`LatencyRecorder` with no expected interval, hand it a `RecordingProbe`, run,
then compute and write the results.

```cpp
const LineConfig config{};
LineSimulator sim{config};

LatencyRecorder recorder;
auto probe = std::make_shared<ch00::RecordingProbe>(recorder);
sim.set_scan_to_divert_probe(probe);

const std::uint64_t completed = sim.run();
```

Before any of that runs, `main` reads `scripts/profile.sh`'s `KEY=value`
lines from stdin and keeps them verbatim, so it never has to re-derive the
host profile itself; the profile label in a results file and the one
`scripts/profile.sh` prints standalone stay identical by construction. After
the run, `measure::ResultsDirScrubGuard` wraps the results directory: its
destructor runs `scripts/scrub-host-ids.py --filter` over every file in that
directory, and because the guard is constructed before the results file is
written and destructed at the end of `main`, the scrub runs after the write
completes. `python3 -I scripts/validate.py` is the check that gates a commit
on a leak; the guard adds an earlier line of defense ahead of it.

**The fragile bits.** This run does not pace parcels to their
`scheduled_arrival` in wall-clock time; `LineSimulator::run()` drives every
parcel through its five stages back to back, as fast as the process can go,
and only uses `scheduled_arrival` as a reference timestamp for a later
chapter's correction logic to compare against. That means this chapter's
scan-to-divert numbers measure the cost of the scan stage's own code, a
probe callback plus a modulo lookup, on an otherwise idle line; a loaded
line under realistic arrival pressure is where later chapters' techniques
start to matter. The default chute count (12) and the default divert lookup
(`barcode % 12`) are `line_simulator.cpp`'s own placeholder routing decision.
A precomputed divert table is the first technique to replace it, and this
chapter's code does not own that decision.

## Build, run, observe

```bash
cd examples/00-running-example && ./demo.sh
```

This builds the default (no-sanitizer) CMake/Conan lane if it is not already
built, runs `ch00_running_example`, and prints the first lines of
`results/baseline.txt`. Look for `sample_count` equal to `parcel_count`
(1000 by default; no coordinated-omission backfill happens in this chapter,
so every parcel contributes exactly one sample), and for `p50_us`, `p99_us`,
and `p999_us` to all sit far under `scan_to_divert_deadline_us`.

## Cross-check

`scan_to_divert_deadline_us` in the results file should equal
`scan_to_diverter_distance_m / belt_speed_m_per_s`, converted to
microseconds, computed by hand. On the house default config (a 1.5 m
scan-to-diverter distance, a 2.0 m/s belt), that is `1.5 / 2.0 = 0.75` s,
or 750000.000 us, which is exactly what the measured run below reports:
the harness is not introducing its own notion of the budget, it is reading
the same two config values the chapter's formula uses.

## The baseline measurement

A run on a 16-core/22-thread laptop CPU, powersave governor, no CPU
pinning or isolation, bare metal, kernel 7.3 (see
`examples/00-running-example/results/baseline.txt` for the full profile
header this run carries):

| Metric | Value |
|---|---|
| `scan_to_divert_deadline_us` | 750000.000 |
| `sample_count` | 1000 |
| `p50_us` | 0.055 |
| `p99_us` | 0.076 |
| `p999_us` | 0.543 |

The histogram in the results file puts 992 of the 1000 samples in a single
narrow bucket just above the minimum, with two samples out past 0.3 us and
one each near 0.37 us and 0.54 us. That shape, almost everything in one
bucket with a thin tail reaching several times further than the bulk, is
what an unloaded, single-threaded, synchronous scan stage looks like: there
is no contention, no I/O, and no queued work to produce a heavier tail yet.
Every percentile here sits more than five orders of magnitude under the
750 ms deadline, which is expected: this run measures the cost of the scan
stage's own code on an idle line. A loaded system, or a different core
class, would report a different distribution against the same budget; these
numbers are specific to the host that produced them and are not a
cross-machine benchmark.

## What you learned

- The running example is one parcel sortation line with five fixed stages; a
  chapter changes one stage's work, never the structure of the line.
- Scan-to-divert's deadline comes from the belt's speed and the
  scan-to-diverter distance, not from a number someone chose.
- `LatencyRecorder` reports percentiles, a histogram, and an eCDF, and
  carries no mean, because a single average would hide the tail a budget
  check exists to catch.
- A chapter wires its own `ScanToDivertProbe` to record a measurement
  without changing the line-simulator itself.

The next chapter keeps this same line and the same metric, and asks what
changes once the correction for coordinated omission, built into
`LatencyRecorder` but left off here, gets turned on.

## For deeper coverage

- Pekka Enberg, *Latency: Reduce Delay in Software Systems* (Manning, 2025):
  this guide's reference work for chapter arc and technique coverage; see
  `bibliography.md`.
- Gil Tene, "How NOT to Measure Latency" (conference talk, Azul Systems):
  the source for the coordinated-omission correction `LatencyRecorder`
  implements.

---

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane; `ctest --preset default` passes,
including the new `RecordingProbe` tests. `examples/00-running-example/demo.sh`
was run on an unprivileged, untuned development host and produced
`results/baseline.txt` with a non-empty, non-uniform distribution whose
percentiles sit under the configured deadline, which is the effect this
chapter claims. The percentile and histogram numbers above are copied
directly from that results file. `python3 -I scripts/validate.py` passes
(host-ids, distribution, harness-cap).*
