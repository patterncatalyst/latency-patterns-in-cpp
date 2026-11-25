---
title: "Dependency pins"
layout: plan
render_with_liquid: false
---

# Dependency pins

The authoritative pin table for every dependency in this repo: the toolchain,
the Jekyll site, and every per-chapter example. Pin to the exact latest
stable release upstream, never a floating range; when a pin has to hold back
from the newest release, record the reason in the same row. This file is a
stub, filled in by the next relay step (the C++23 toolchain bootstrap) and
updated every time a chapter adds a dependency.

## Site toolchain (filled by r00; see `Gemfile`)

| Dependency | Pin | Reason to hold (if any) |
|---|---|---|
| jekyll | 4.4.1 | |
| kramdown-parser-gfm | 1.1.0 | |
| rouge | 4.7.0 | jekyll 4.4.1 requires `rouge >= 3.0, < 5.0` |
| jekyll-feed | 0.18.0 | |
| jekyll-sitemap | 1.4.0 | |
| jekyll-seo-tag | 2.9.1 | |
| webrick | 1.9.2 | |

## C++23 toolchain (filled by r00)

User-space installs only, unpacked under `.toolchain/` (gitignored) by
`scripts/bootstrap-toolchain.sh`, never the Fedora-packaged compiler on the
host `PATH`: this repo's measured binaries are never built with a toolchain
Fedora's package manager can silently roll forward. `eval
"$(scripts/toolchain-env.sh)"` puts the pinned tools first on `PATH` for a
build. Conan itself is a `pip install --user` into `~/.local/bin`, already
user-space and independent of the Fedora package set. See `versions.env` for
the single-source-of-truth copy of every version below.

| Dependency | Pin | Reason to hold (if any) |
|---|---|---|
| Clang / LLVM | 23.1.6 | surveyed 2026-11-24, upstream latest stable; user-space install, not Fedora's 23.1.5 package |
| CMake | 4.5.1 | surveyed 2026-11-24, upstream latest stable; user-space install from Kitware |
| Ninja | 1.13.2 | surveyed 2026-11-24, upstream latest stable; user-space install from the ninja-build GitHub releases |
| Conan | 2.34.0 | surveyed 2026-11-24, upstream latest stable (PyPI); `pip install --user` |
| GoogleTest / GoogleMock | 1.18.0 | surveyed 2026-11-24, upstream latest stable; pinned in `conanfile.py`, Abseil pinned transitively |
| spdlog | 1.17.0 | surveyed 2026-11-24, upstream latest stable; pinned in `conanfile.py` |

## Rust toolchain (Ch5, Ch6, Ch9 only; filled by r03)

User-space install only, under `.toolchain/rust/` (gitignored, covered by the
existing `.toolchain/` pattern) with its own `RUSTUP_HOME` and `CARGO_HOME`,
installed by `scripts/bootstrap-rust.sh`, never the host's own `~/.rustup` or
`~/.cargo`: the Rust lane is never built with a toolchain this repo does not
pin exactly. `eval "$(scripts/rust-env.sh)"` puts the pinned `rustc`/`cargo`
first on `PATH` for a build. `rust-toolchain.toml` at the repo root carries
the same pin so `rustup` auto-selects it, in CI as well as on the host. See
`versions.env` for the single-source-of-truth copy of the version below.

| Dependency | Pin | Reason to hold (if any) |
|---|---|---|
| rustc / cargo | 1.100.0 | surveyed 2026-11-25, upstream latest stable; user-space install via rustup, not the host's rustup/cargo |

## Per-chapter example dependencies

Filled in as each chapter ships, one block per chapter, naming every
third-party library on that chapter's critical path and confirming its OSS
license.
