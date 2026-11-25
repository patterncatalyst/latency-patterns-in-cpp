#!/bin/sh
# rust-env.sh - print the env vars that select this repo's user-space Rust
# toolchain over whatever the host happens to have on PATH or in ~/.cargo.
#
# This repo pins rustc/cargo to an exact upstream stable release unpacked
# under .toolchain/rust/ by scripts/bootstrap-rust.sh (see rust-toolchain.toml,
# versions.env, and _plans/dependency-pins.md for the pin). Nothing here
# touches the host: no dnf, no sudo, no edits outside this repo checkout, and
# no write to the host's own ~/.rustup or ~/.cargo.
#
# Usage (this script is never sourced directly, so it can resolve its own
# location from $0 without the sourced-script ambiguity):
#   eval "$(scripts/rust-env.sh)"
#   cargo test --manifest-path examples/_rust-probe/Cargo.toml
#
# Output: one `export KEY=value` line per variable. Nothing is printed that
# names this development host (see CLAUDE.md, "Public-repo host-id scrub
# rule"); every path is resolved at run time relative to this script.

set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH='' cd -- "${script_dir}/.." && pwd)
rust_toolchain_dir="${repo_root}/.toolchain/rust"
rustup_home="${rust_toolchain_dir}/rustup"
cargo_home="${rust_toolchain_dir}/cargo"

if [ ! -x "${cargo_home}/bin/rustup" ]; then
  printf 'rust-env.sh: missing %s/bin/rustup - run scripts/bootstrap-rust.sh first\n' "$cargo_home" >&2
  exit 1
fi

printf 'export RUSTUP_HOME=%s\n' "$rustup_home"
printf 'export CARGO_HOME=%s\n' "$cargo_home"
printf 'export PATH=%s/bin:$PATH\n' "$cargo_home"
