#!/usr/bin/env bash
#
# examples/03-eliminating-work/demo.sh
#
#   ./demo.sh                 # install deps (if needed) + build + run + show the result
#   ./demo.sh build           # just build
#   ./demo.sh run             # just run (requires a prior build)
#
# Builds the default (no-sanitizer) lane, runs the chapter's example binary,
# and prints the before/after scan-to-divert distributions plus the
# chute_mismatches behavioral check. Host-only, unprivileged (see CLAUDE.md,
# "Never change the development host"): no sudo, no package install outside
# this repo's own .toolchain/ and build/ directories.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)" && cd "$SCRIPT_DIR"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
BINARY="$REPO_ROOT/build/examples/03-eliminating-work/cpp/ch03_eliminating_work"
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
  "$REPO_ROOT/scripts/profile.sh" ch03-eliminating-work | \
    "$BINARY" "$RESULTS_DIR" "$REPO_ROOT/scripts/scrub-host-ids.py"
  echo
  echo "--- examples/03-eliminating-work/results/eliminating-work.txt ---"
  grep -E '^(chute_mismatches|before_(sample_count|p50_us|p99_us|p999_us)|after_(sample_count|p50_us|p99_us|p999_us))=' \
    "$RESULTS_DIR/eliminating-work.txt"
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
