# CLAUDE.md (Latency: A Practical Guide in C++)

## Restart note

If you are picking this up cold: this repo scaffolded on 2026-11-24 from an
approved relay plan for "Latency: A Practical Guide in C++". Read
`_plans/v1-outline.md` and `_plans/iteration-plan.md` before writing anything.
This repo is a clean start. It is not a port of `low-latency-engineering` and
never imports from it; that repo is paused and unrelated history.

## What this is

A chapter-based Jekyll site teaching latency engineering through original
C++23 examples, one latency pattern per chapter, in the lgtm house style.
Three chapters add a Rust lane next to the C++23 lane in the tabbed
code-example component, because ownership changes what those three
techniques have to prove. Every chapter measures the same spine metric on the
same running example. Chapters live in `_docs/`, their examples in
`examples/`, figures in `assets/diagrams/`.

## The spine

- **Backbone language: C++23.** Every chapter ships a C++23 example built
  with Clang, CMake, and Conan, tested with GoogleTest. Idiomatic first: the
  standard library and the established libraries before a hand-rolled
  structure. Where a technique needs something non-idiomatic (a busy-spin
  loop, manual memory layout, a lock-free structure), the prose says why, and
  the rest of the example stays idiomatic.
- **Rust appears in exactly three chapters**: Ch5 (wait-free synchronization),
  Ch6 (exploiting concurrency), and Ch9 (asynchronous processing), as a
  tabbed side-by-side using the `code-tabs.html` include, never a parallel
  `rust/` directory anywhere else. Ch4 gets a prose ownership sidebar only, no
  code. A Rust lane in a chapter that teaches no ownership lesson is noise;
  that is the failure mode this rule exists to block.
- **Spine metric: scan-to-divert** (sense, decide, act), measured end to end on
  every example. The budget is physical: `conveyor_speed * distance(scan,
  diverter) = deadline`. Ask of every technique whether it fits that budget.

## Domain

The running example is a parcel sortation line: checkweigh, dimension, scan,
label, sort. One example per chapter, with sub-techniques inside that example
as measured comparison lanes, not as separate projects (Ch7's caching chapter
is one cache-in-front-of-routing example measured across LRU, LFU, FIFO, and
SIEVE lanes, not four examples). The belt never waits for a slow consumer, so
coordinated omission is physical here, not a definition to memorize.

## Sources, reference only

Enberg's *Latency* (Manning, 2025) is a reference work for chapter arc and
coverage, nothing more. Do not port, translate, adapt, or paraphrase it or any
other source. Design every example from the technique itself; if an example
resembles something you have read, change the workload or drop it. See
`bibliography.md` for the full reference list.

## Measurement discipline

- Report a distribution (p50/p99/p99.9 plus a histogram or eCDF), never a
  mean. A mean may appear only as a labelled modelling input (Little's Law,
  Amdahl's Law, Ch2), inside a `model` block, never as a reported latency.
- Verify the effect, not the build. A clean build or a passing smoke test is a
  checkpoint, never the finish line.
- Label every number with its machine or VM profile: CPU class, governor,
  pinning, isolation, bare metal versus VM. A VM number and a bare-metal
  number are not comparable; say so wherever both appear.
- Run the premise probe before building a lane. `_plans/premise-probe.md`
  records the measured effect size per technique before any harness or lane
  gets built around it; an effect too small to see on an untuned laptop gets
  labelled exploratory, not hidden.

## Never change the development host

This repo's scripts and chapters run entirely as an unprivileged user on the
host. No script here may touch the kernel, a boot parameter, `sysctl`,
`tuned`, the CPU governor, hugepages, or a system package, and none may
invoke `sudo`. All privileged and real-time work, including Ch10 (taming the
host), isolcpus, `nohz_full`, IRQ affinity, RT scheduling, and cyclictest,
happens inside throwaway `lowlat-*` KVM virtual machines through
`lgtm-systems-programming`, never on the machine this repo is built on.

## Public-repo host-id scrub rule

This repo is public, so no tracked file may name the development machine or
its owner, including this file:

- No absolute home-directory path (`/home/...`, `/Users/...`) and no account
  name. Write `<repo>/...` or `~/...`.
- No hostname, and no laptop or CPU model or model number. The CPU is a
  class ("8-core/16-thread laptop CPU"), never the part.
- No host kernel build number or build date, no `/etc/machine-id`, no DMI
  serial or UUID, no MAC or IP address.
- `scripts/profile.sh` records only the generic profile label described
  above. `scripts/scrub-host-ids.py` holds the one replacement table (the
  account name is read at run time, never written into it) and supports
  `--filter` (scrub stdin) and `--print-rules` (emit `git filter-repo
  --replace-text` rules for a history rewrite, never committed). `python3
  scripts/validate.py` fails the build on a leak.

## Scope discipline

v1 is 12 chapters (Ch0 through Ch11, including the catalog index), frozen in
`_plans/v1-outline.md`. One running example per chapter, sub-techniques as
measured lanes inside it, not as separate example projects. Everything in
`_plans/v1-outline.md`'s v2 backlog (predictive techniques, replication and
consistency models, distributed cache coherency, internode colocation,
kernel-bypass transport, a transport-by-latency-budget survey, a workshop
spin-off) is deferred, not built, not scaffolded ahead of time. Cross-
reference sparingly: name the pattern a later chapter needs, not its chapter
number, so chapters stay reorderable. Cap the harness against the content; a
mega `validate.py` that outgrows the chapters it checks is the specific
failure this rule exists to prevent. Decks are the last iteration, after
every chapter ships. A workshop format is extractable from this material
later; it is not built now.

## Per-chapter deliverables

Each of Ch0 through Ch11 ships:

- At least one paired `name.svg` + `name.excalidraw` diagram.
- One runnable C++23 example under `examples/NN-name/cpp/` (plus `rust/` only
  for Ch5, Ch6, Ch9), building clean under Clang/CMake/Conan with GoogleTest,
  ASan and UBSan clean, TSan green for the concurrency chapters.
- One automated walkthrough, driven from the example's `README.md`.
- One profile-labelled `results/` directory holding p50/p99/p99.9 and a
  histogram or eCDF, one per measured lane.

## Dependencies

OSS only. Pin every dependency to its exact latest stable release upstream,
never a floating range; record the pin and, if a pin has to hold back from
the newest release, the reason next to it (see `Gemfile` for a worked
example: rouge held at 4.7.0 because jekyll 4.4.1 caps it below 5.0).
Idiomatic first, in every language: the standard library and the established
libraries before a hand-rolled structure. See `_plans/dependency-pins.md` for
the full pin table.

## Voice

Write in the Geewax register: lead with the situation, then its consequence.
No em-dash. No rule-of-three lists used as a rhetorical device. No
stacked-noun subjects ("the cache invalidation strategy implementation"
instead of "how the cache invalidates"). No AI tells, no meta-narration about
what the text is about to do. No reassurance words: "honestly," "honest,"
"truly," "genuinely," and "really" do not appear in prose.

## Skills to use

| Task | Skill |
|---|---|
| Scaffold or change the site | `lgtm-jekyll` |
| Write or revise a chapter | `lgtm-tutorial` |
| Make a figure | `lgtm-diagram-generator` |
| Scaffold or extend the C++23 toolchain | `lgtm-cpp` |
| Build the conference decks (last iteration) | `lgtm-presentation` |
| RT kernel, partitioning, or the `lowlat-*` VM lab | `lgtm-systems-programming` |
| Idiomatic-code pass before a chapter ships | `lgtm-idiomatic-review` |
| Voice pass before a chapter ships | `lgtm-professional-voice` |
| Host-id scrub pass | `lgtm-host-scrub` |
| Commits, releases, repo setup | `lgtm-github` |

## Releases

Conventional Commits, scopes `chN` / `rNN.x`. Each iteration is its own
`release/rNN` branch and PR, branched from an updated `main`, squash-merged
with the branch deleted, then tagged `rNN.x` on `main`. An iteration ships as
its merged PR plus the tag and a notes-only GitHub release, no other
artifact. The site deploys to GitHub Pages from `main`. See
`_plans/iteration-plan.md` for what ships in each `rNN`.

## Do not

- Do not report a number you did not measure, or omit its machine/VM profile.
- Do not use a mean as a reported latency anywhere.
- Do not add a Rust lane outside Ch5, Ch6, Ch9.
- Do not split a chapter's one running example into several example projects.
- Do not build v2-backlog material (see Scope discipline) ahead of its time.
- Do not run privileged, kernel, or RT work anywhere but a throwaway
  `lowlat-*` VM. No `sudo` in any script in this repo.
- Do not reproduce, port, or adapt text or code from a reference source.
- Do not let a tracked file name the development host, its owner, or its
  hardware (see Public-repo host-id scrub rule).
- Do not let `validate.py` grow past what one person can read in one sitting;
  the prior repo's `validate.py` hit 10,000+ lines and that is the lesson
  this rule exists to avoid repeating.
