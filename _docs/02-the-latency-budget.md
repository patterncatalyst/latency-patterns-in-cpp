---
title: "The latency budget"
order: 2
part: "Measuring Latency"
description: "Little's Law and Amdahl's Law as planning tools, the six sources of latency on this line, and the first measured per-stage decomposition of the scan-to-divert budget."
duration: 25 minutes
---

## Learning objectives

By the end of this chapter you can:

- Apply Little's Law to estimate how many parcels occupy a belt segment at
  once, given an arrival rate and a residence time, before any code runs.
- Apply Amdahl's Law to find the speedup ceiling a single parallelizable
  stage can offer, as a planning estimate, not a measured result.
- Name the six sources of latency on this line and say which ones this
  guide can measure from an untuned host and which it cannot.
- Read a measured per-stage decomposition and connect it to the physical
  scan-to-divert deadline the running example and harness chapter derived.

## Diagram

{% include excalidraw.html file="budget-decomposition" alt="The five stages, checkweigh, dimension, scan, label, sort, in process order, each labelled with its measured p50 contribution; scan, label, and sort are bracketed as the scan-to-divert window, with a note comparing the measured total against the 750 millisecond physical deadline." caption="Figure 2.1: Where the scan-to-divert budget goes, stage by stage. Scan dominates; the measured total still lands far inside the physical deadline." %}

## Why this matters

The prior chapter put a real cost into the scan stage and showed what a
loaded distribution looks like. Every other stage, checkweigh, dimension,
label, sort, stayed at the simulator's default: a no-op. That was enough to
ask whether a mean or a percentile tells the truth about one stage's tail.
It said nothing about where a budget goes once every stage does real work,
and nothing about reasoning about a budget before a line of code runs.

This chapter is a modelling chapter. Before any technique gets built, two
tools from queueing theory let an engineer reason about a budget on paper:
Little's Law, for how many parcels occupy a segment of belt at once, and
Amdahl's Law, for the ceiling on speedup when only part of a pipeline can
run in parallel. Neither tool measures anything by itself; each turns a
measured or assumed input into a planning number. This chapter also gives
every stage a real, timed cost for the first time, which is what makes the
two models' inputs real numbers instead of guesses.

The code is in `examples/02-latency-budget/`. The run script there builds,
sets up, and runs it; its `README.md` covers what it does and how to drive
it.

## Little's Law: how many parcels are in flight

Little's Law states `L = lambda * W`: the average number of items in a
system (`L`) equals the arrival rate (`lambda`) times the average time each
item spends in that system (`W`). Applied to the belt segment between the
scanner and the diverter, `lambda` is the arrival rate and `W` is the
scan-to-divert deadline itself, 0.75 seconds at this guide's house belt
geometry (see the running example and harness chapter, "The scan-to-divert
metric and its budget"): that deadline is exactly how long a parcel occupies
that segment, regardless of how fast or slow any stage's own processing
runs.

```model
# Little's Law (L = lambda * W): a planning estimate, never a measured
# occupancy count. lambda = arrival rate, W = the scan-to-divert deadline.
w_s = 0.750000
lambda_default_per_s = 10.0
L_default_estimate = 7.5

lambda_loaded_per_s = 2000.0
L_loaded_estimate = 1500.0
```

At the house default arrival rate of 10 parcels per second, the model says
roughly 7 or 8 parcels occupy that 1.5 meter segment at any instant, a
believable number: the diverter mechanism and its upstream buffer need to
hold about that many slots. At the 2000-parcels-per-second rate the prior
chapter used to stress the scan stage, the same model says 1500 parcels
occupy the same segment, physically absurd for 1.5 meters of belt. That is
the model doing its job: 2000 parcels per second was always a software
stress parameter, not a literal arrival rate this belt could sustain, and
Little's Law catches that mismatch before any code has to run to discover it
the hard way.

## Amdahl's Law: the ceiling on parallelizing one stage

Amdahl's Law asks a narrower question: if a fraction `p` of a pipeline runs
on `workers` parallel workers and the rest stays serial, what is the most
splitting the parallel part can ever buy? The formula is
`speedup = 1 / ((1 - p) + p / workers)`; its most useful property is the
ceiling: as `workers` grows without bound, speedup approaches `1 / (1 - p)`
and no further.

This chapter's own measurements supply `p`: the scan stage's share of the
scan_to_divert total (scan plus label plus sort, the three stages inside the
deadline window). Scan's mean cost is 5.495 microseconds against a
scan_to_divert mean of 6.700 microseconds, so `p` is about 0.82.

```model
# Amdahl's Law (speedup = 1 / ((1 - p) + p / workers)): a planning estimate.
# p is this run's measured scan-stage mean over its scan_to_divert mean
# (both means are modelling inputs only, never reported as latencies).
p_parallel_fraction = 0.8201
speedup_at_2_workers = 1.695
speedup_at_4_workers = 2.598
speedup_at_8_workers = 3.541
speedup_ceiling = 5.558
```

Even if scan's decode work could be perfectly split across an unlimited
number of workers, the scan-to-divert span on this line could never run
faster than about 5.6 times its current speed, because label and sort stay
serial and already account for the rest. Doubling the worker count from four
to eight buys less than one extra unit of speedup, a shrinking return this
chapter's own numbers make concrete. Nothing in this chapter's example
splits scan across workers; that is a concurrency chapter's concern, not
this one's. What Amdahl's Law gives here is a ceiling worth knowing before
committing to that work: a technique that promises more than 5.6x on this
pipeline is promising something the arithmetic already rules out.

## Sources of latency on this line

Six sources of latency recur across nearly every system, and this line
carries every one of them somewhere.

**Physics.** The 750 millisecond deadline is itself a physics fact: distance
divided by belt speed. No code path's execution time is "the belt moving,"
so this guide cannot measure physics directly; it only shows up as the
budget the other sources fit inside.

**CPU and hardware.** Every stage's own compute cost, the checksum loop in
`scan_work`, the averaging loops in `checkweigh_work` and `dimension_work`,
the formatting in `LabelRenderer`, is CPU and hardware cost: branch
prediction, cache behavior, instruction throughput. This is the one source
this chapter measures directly and in full, stage by stage, below.

**Virtualization.** A hypervisor can steal CPU time from a guest at moments
the guest's clock cannot see. This guide's measurement host runs bare metal
(see the profile header in every results file), so this chapter has nothing
to show for it; it is named as a source this book cannot demonstrate
without a VM, not one that does not exist.

**OS and drivers.** Scheduling, interrupts, syscalls, and a device driver's
own latency live here. The distribution chapter's stall, an
application-level `sleep_for`, stood in for a cost this category actually
describes (a spooler blocking); this chapter's own run-to-run jitter is this
source leaking into otherwise identical measurements, without this book
isolating it on its own.

**Managed runtime.** A garbage collector's pause or a JIT's warm-up tax is a
real cost in many stacks. It does not apply here: this guide's backbone
language is C++23, compiled ahead of time, with nothing to pause. It is
named for readers coming from a managed language, who should not expect to
find it measured anywhere in this guide.

**Application.** The work actually requested is the one source entirely
inside an engineer's own control. `scan_work`'s checksum loop and
`LabelRenderer`'s formatting are both application choices; a later
chapter's "eliminating work" replaces one of them with less work, not less
hardware.

Two of six, CPU/hardware and application, are what this chapter's measured
numbers are made of. The rest are real and named, mostly out of reach of an
untuned laptop with no VM, no managed runtime, and no hypervisor in the loop.

## The measured per-stage decomposition

`examples/02-latency-budget/cpp/` times all five stages for the first time
(see `ch02::PipelineTimingHarness`), over 5000 parcels. Reading
`examples/02-latency-budget/results/budget-decomposition.txt`:

| Stage | p50 (microseconds) | p99 (microseconds) | p99.9 (microseconds) |
|---|---|---|---|
| checkweigh | 0.047 | 0.140 | 0.167 |
| dimension | 0.078 | 0.247 | 0.294 |
| scan | 3.312 | 15.958 | 19.358 |
| label | 0.914 | 2.362 | 5.190 |
| sort | 0.022 | 0.070 | 0.131 |
| scan_to_divert (scan + label + sort) | 4.235 | 17.905 | 22.544 |

Scan dominates every other stage's p50 by an order of magnitude or more: the
barcode checksum is real, deliberate work, and it shows. Checkweigh and
dimension run before a parcel reaches the scanner, so neither is part of the
scan-to-divert window the deadline bounds; they are measured anyway, because
"sources of latency" needs numbers from every stage, not just the three
inside one particular budget. `scan_to_divert` sums scan, label, and sort
for the same parcel, never re-measured end to end, so the parts and the
whole agree by construction (see "How the code works" below). Even at
p99.9, the slowest 0.1 percent of parcels, `scan_to_divert` sits at 22.544
microseconds, roughly 33,000 times under the 750000.000 microsecond
deadline: every stage doing real work still leaves nearly the whole
physical budget unspent.

## How the code works

**Timing every stage.** `ch02::PipelineTimingHarness::install` registers one
timing wrapper per stage via `LineSimulator::set_stage_work`, one call per
stage rather than one callback that branches on which stage it was given:

```cpp
sim.set_stage_work(Stage::Scan,
                    [this, work = std::move(workloads.scan)](Parcel& parcel, Stage stage) {
                        time_stage(parcel, stage, work);
                    });
```

`time_stage` records the elapsed wall time around the real workload into
that stage's own sample vector, and when the stage being timed is Sort, the
last stage in order, it sums the three just-recorded values for scan, label,
and sort into the `scan_to_divert` lane, never by re-timing start to finish,
so "the parts add up to the total" is true by construction rather than
something a later bug could silently break.

**The five workloads.** Checkweigh and dimension (`ch02::checkweigh_work`,
`ch02::dimension_work`) average several synthetic noisy readings into one
value, the measurable cost of filtering a noisy signal, never the sensor's
own settle time. Scan (`ch02::scan_work`) runs a barcode checksum, the
heaviest cost by design, chosen so Amdahl's Law above has a real `p` to work
with. Label (`ch02::LabelRenderer`) and sort (`ch02::SortDispatcher`) are
small stateful types rather than free functions: each keeps its own state
across calls (a rendered print line, a running per-chute count), the way a
real label printer's buffer or a diverter controller's occupancy tracker
would.

**The model math.** `ch02::compute_mean` lives outside
`measure::LatencyRecorder` for the same reason the distribution chapter's
version does (see "Why a distribution, not a mean" in that chapter): a mean
is never a reported latency in this guide, and this function exists only to
supply Little's Law and Amdahl's Law their modelling inputs.
`ch02::little_law_in_flight_estimate` and `ch02::amdahl_speedup` are small
free functions, each with its own GoogleTests covering the house-default
belt geometry by hand, the zero-parallel-fraction edge case, and the
Amdahl ceiling that holds even at a thousand workers.

**The fragile bits.** Every stage's cost is real wall-clock time on a
shared, untuned host scheduler, so a re-run produces numbers close to, but
not identical with, the ones quoted above; the five stages' own constants
(decode iterations, noise sample counts) are chosen so scan dominates
clearly, not values a real sortation line would necessarily use.

## Build, run, observe

```bash
cd examples/02-latency-budget && ./demo.sh
```

Look for `scan_p50_us` sitting an order of magnitude above every other
stage's `p50_us`, and for every `scan_to_divert_*_us` figure sitting many
orders of magnitude under `scan_to_divert_deadline_us`.

## Cross-check

`scan_to_divert_p50_us` should sit close to, but not exactly at,
`scan_p50_us` plus `label_p50_us` plus `sort_p50_us` (3.312 + 0.914 + 0.022
= 4.248 against a measured 4.235): percentiles of a sum are not the sum of
percentiles, which is why `scan_to_divert`'s own percentiles come from the
summed per-parcel samples, never derived from the three stages' percentiles
after the fact. `model_little_law_default_L_estimate` should equal
`model_little_law_default_lambda_per_s` times `model_little_law_w_s` by hand
(10 times 0.75 is 7.5), the same hand check every prior chapter's deadline
cross-check uses.

## What you learned

- Little's Law (`L = lambda * W`) turns an arrival rate and a residence time
  into an occupancy estimate before any code runs, and can expose an input
  that describes a physically impossible line.
- Amdahl's Law (`speedup = 1 / ((1 - p) + p / workers)`) puts a ceiling on
  what parallelizing one stage can ever buy, from that stage's own measured
  share of the total.
- Six sources of latency, physics, CPU/hardware, virtualization, OS/drivers,
  managed runtime, and application, recur everywhere; this guide can
  measure two of them directly from an untuned host.
- A per-stage decomposition shows where a budget actually goes: on this
  run, scan alone accounts for most of the measured cost, and the measured
  total still lands far inside the physical deadline.

The next chapter stops modelling and starts optimizing: it targets the scan
stage's decode path directly, the same stage this chapter's own numbers
just named as the heaviest one on the line.

## For deeper coverage

- John D. C. Little, "A Proof for the Queuing Formula: L = λW" (Operations
  Research, 1961): the original proof behind the law this chapter applies.
- Gene Amdahl, "Validity of the Single Processor Approach to Achieving
  Large Scale Computing Capabilities" (AFIPS, 1967): the original paper
  behind the law this chapter applies.
- Pekka Enberg, *Latency: Reduce Delay in Software Systems* (Manning, 2025):
  this guide's reference work for chapter arc and technique coverage; see
  `bibliography.md`.

---

*Verification status: <span class="status status--verified">verified</span>.
Built with the default CMake/Conan lane; `ctest --preset default` passes (47
tests total, including the new `PipelineTimingHarness`, stage-workload, and
`budget_model` GoogleTests; the two prior chapters' tests still pass
unchanged). The ASan/UBSan lane (`cmake --preset asan-ubsan`) builds clean,
its own `ctest --preset asan-ubsan` passes, and the example binary runs
clean under that lane with no sanitizer report.
`examples/02-latency-budget/demo.sh` ran on an unprivileged, untuned
development host and produced `results/budget-decomposition.txt` with scan
as the dominant per-stage cost and `scan_to_divert` far under the 750
millisecond deadline, the effect this chapter claims. Every measured number
above was copied directly from that results file; the Little's Law and
Amdahl's Law figures are clearly marked modelling estimates inside `model`
blocks, never reported as measured latencies. `python3 -I
scripts/validate.py` passes (host-ids, distribution, harness-cap).*
