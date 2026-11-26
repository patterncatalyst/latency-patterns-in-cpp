#!/usr/bin/env bash
#
# examples/09-async-processing/demo.sh
#
#   ./demo.sh                 # install deps (if needed) + build + run + show the result
#   ./demo.sh build           # just build (C++)
#   ./demo.sh run             # just run the C++ binary (requires a prior build)
#   ./demo.sh rust            # build (release) + run the Rust bench
#
# Builds the default (no-sanitizer) C++ lane, runs the chapter's example
# binary, and prints the sync-inline, off-path, and backpressure-demo
# results. Host-only, unprivileged (see CLAUDE.md, "Never change the
# development host"): no sudo, no package install outside this repo's own
# .toolchain/ and build/ directories, no docker. The off-path OTLP export
# the chapter's background consumer can optionally make is controlled by
# the CH09_OTLP_ENDPOINT environment variable (unset here, so it never
# runs); see observability/README.md for bringing up the LGTM stack via
# lgtm-docker-stack and pointing that variable at it. The measured numbers
# below never depend on that stack being up.
#
# The Rust lane lives alongside this one under rust/ and is driven
# separately through scripts/rust-env.sh and cargo (the `rust` subcommand
# here, or rust/ directly): the two toolchains stay independent so a
# missing Rust toolchain never blocks the C++ build this script drives.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)" && cd "$SCRIPT_DIR"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
BINARY="$REPO_ROOT/build/examples/09-async-processing/cpp/ch09_async_processing"
RUST_BINARY="$SCRIPT_DIR/rust/target/release/async_bench"
SUBCOMMAND="${1:-all}"

build() {
  if [ ! -x "$REPO_ROOT/.toolchain/llvm-23.1.6/bin/clang++" ]; then
    echo "demo.sh: toolchain missing; run scripts/bootstrap-toolchain.sh first" >&2
    exit 1
  fi
  # shellcheck disable=SC1090
  eval "$("$REPO_ROOT/scripts/toolchain-env.sh")"
  if [ ! -f "$REPO_ROOT/build/conan_toolchain.cmake" ]; then
    conan install "$REPO_ROOT" -pr:a "$REPO_ROOT/conan/profiles/clang23" \
      --build=missing -of "$REPO_ROOT/build" -s build_type=Debug
  fi
  (cd "$REPO_ROOT" && cmake --preset default && cmake --build --preset default)
}

run() {
  mkdir -p "$RESULTS_DIR"
  "$REPO_ROOT/scripts/profile.sh" ch09-async-processing | \
    "$BINARY" "$RESULTS_DIR" "$REPO_ROOT/scripts/scrub-host-ids.py"
  echo
  echo "--- examples/09-async-processing/results/async-processing.txt ---"
  grep -E '^(io_uring_available|offpath_writer_backend|batch_capacity|sync_(item_count|wall_time_us|throughput_items_per_sec|sample_count|p50_us|p99_us|p999_us)|offpath_(item_count|wall_time_us|throughput_items_per_sec|shed_count|high_water_mark|queue_capacity|sample_count|p50_us|p99_us|p999_us|vs_sync_mismatch_count)|backpressure_(item_count|shed_count|high_water_mark|queue_capacity|sample_count|p50_us|p99_us|p999_us|missing_from_audit_count)|flush_(blocking|io_uring)_(p50_us|p99_us|p999_us))=' \
    "$RESULTS_DIR/async-processing.txt"
}

rust_bench() {
  if [ ! -x "$REPO_ROOT/.toolchain/rust/cargo/bin/rustup" ]; then
    echo "demo.sh rust: Rust toolchain missing; run scripts/bootstrap-rust.sh first" >&2
    exit 1
  fi
  # shellcheck disable=SC1090
  eval "$("$REPO_ROOT/scripts/rust-env.sh")"
  cargo build --release --manifest-path "$SCRIPT_DIR/rust/Cargo.toml"
  mkdir -p "$RESULTS_DIR"
  "$REPO_ROOT/scripts/profile.sh" ch09-async-processing-rust | "$RUST_BINARY" "$RESULTS_DIR"
  python3 -I "$REPO_ROOT/scripts/scrub-host-ids.py" --filter \
    < "$RESULTS_DIR/async-processing-rust.txt" > "$RESULTS_DIR/async-processing-rust.txt.scrubbed"
  mv "$RESULTS_DIR/async-processing-rust.txt.scrubbed" "$RESULTS_DIR/async-processing-rust.txt"
  echo
  echo "--- examples/09-async-processing/results/async-processing-rust.txt ---"
  grep -E '^rust_' "$RESULTS_DIR/async-processing-rust.txt"
}

case "$SUBCOMMAND" in
  build) build ;;
  run) run ;;
  rust) rust_bench ;;
  all) build; run ;;
  *)
    echo "usage: $0 [build|run|rust]" >&2
    exit 1
    ;;
esac
