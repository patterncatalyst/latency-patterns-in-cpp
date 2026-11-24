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

## C++23 toolchain (TBD, next relay step)

| Dependency | Pin | Reason to hold (if any) |
|---|---|---|
| Clang / LLVM | TBD | |
| CMake | TBD | |
| Conan | TBD | |
| GoogleTest / GoogleMock | TBD | |
| Ninja | TBD | |

## Rust toolchain (Ch5, Ch6, Ch9 only; TBD)

| Dependency | Pin | Reason to hold (if any) |
|---|---|---|
| rustc / cargo | TBD | |

## Per-chapter example dependencies

Filled in as each chapter ships, one block per chapter, naming every
third-party library on that chapter's critical path and confirming its OSS
license.
