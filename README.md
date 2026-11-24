# Latency: A Practical Guide in C++

A chapter-based guide to latency engineering in C++23. Each chapter takes one
pattern, applies it to a single running example (a parcel sortation line),
and measures it against the same metric: scan-to-divert, the time from
sensing a parcel to acting on it. Three chapters add a Rust lane next to the
C++23 lane, in a tabbed code component, where ownership changes what the
technique has to prove.

**Live site:** not yet published; this repo is still scaffolding (iteration
r00). See `_plans/iteration-plan.md` for what ships next.

## What this is

Twelve chapters, each with one runnable example, one latency pattern, and one
profile-labelled set of measurements. No chapter reports a mean as a latency;
every number is a distribution (p50/p99/p99.9) with a histogram or eCDF,
labelled with the machine or VM profile that produced it. One chapter, on
taming the host, runs in a throwaway virtual machine; every other chapter runs
unprivileged, on the host it is written on.

## Prerequisites

Fedora Linux, Clang, CMake, and Conan for the C++23 examples; Jekyll (via
Bundler) to read the site locally. Chapter 0 covers setup in full once it
ships; see `_plans/dependency-pins.md` for the exact toolchain pins as they
land.

## Build and test

Per-chapter build and test commands land with each chapter's example under
`examples/NN-name/`. None have shipped yet.

## Reading the site locally

```sh
bundle install
bundle exec jekyll serve --baseurl ""
```

## Layout

| Path | Contents |
| --- | --- |
| `_docs/` | Chapters (`NN-*.md`) |
| `_parts/` | Part landing pages |
| `examples/` | One runnable example per chapter, C++23 (three chapters add a Rust lane) |
| `assets/diagrams/` | Paired `.svg` + `.excalidraw` figures |
| `scripts/` | `profile.sh`, `scrub-host-ids.py`, `validate.py`: the measurement and host-id guardrails |
| `_plans/` | Build plans and the iteration schedule (not published) |

## Status

Scaffolding only: the site structure, `CLAUDE.md`, the plans, and the
guardrail scripts are in place. No chapter has shipped yet.

## License

Code is licensed under the Apache License 2.0; see [LICENSE](LICENSE).
Written and visual content is licensed under CC BY 4.0; see
[LICENSE-docs.md](LICENSE-docs.md).
