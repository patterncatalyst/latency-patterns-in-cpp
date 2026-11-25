#!/usr/bin/env bash
#
# examples/05-wait-free-sync/demo.sh
#
#   ./demo.sh                 # install deps (if needed) + build + run + show the result
#   ./demo.sh build           # just build
#   ./demo.sh run             # just run (requires a prior build)
#
# Builds the default (no-sanitizer) C++ lane, runs the chapter's example
# binary, and prints the mutex-queue and wait-free-ring handoff results.
# Host-only, unprivileged (see CLAUDE.md, "Never change the development
# host"): no sudo, no package install outside this repo's own .toolchain/
# and build/ directories. Neither lane pins a thread or touches host
# configuration; both run on whatever cores the scheduler hands them.
#
# The Rust lane lives alongside this one under rust/ and is driven
# separately (see rust/README.md or this directory's README.md), through
# scripts/rust-env.sh and cargo, not through this script: the two
# toolchains stay independent so a missing Rust toolchain never blocks the
# C++ build this script drives.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)" && cd "$SCRIPT_DIR"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
BINARY="$REPO_ROOT/build/examples/05-wait-free-sync/cpp/ch05_wait_free_sync"
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
  "$REPO_ROOT/scripts/profile.sh" ch05-wait-free-sync | \
    "$BINARY" "$RESULTS_DIR" "$REPO_ROOT/scripts/scrub-host-ids.py"
  echo
  echo "--- examples/05-wait-free-sync/results/wait-free-sync.txt ---"
  grep -E '^(handoff_(mutex|ring)_(items_sent|items_received|dropped_count|out_of_order_count|duplicate_count|full_retries|sample_count|p50_us|p99_us|p999_us))=' \
    "$RESULTS_DIR/wait-free-sync.txt"
}

case "$SUBCOMMAND" in
  build) build ;;
  run) run ;;
  all) build; run ;;
  *)
    echo "usage: $0 [build|run]" >&2
    exit 1
    ;;
esac
