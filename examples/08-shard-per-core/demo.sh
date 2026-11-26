#!/usr/bin/env bash
#
# examples/08-shard-per-core/demo.sh
#
#   ./demo.sh                 # install deps (if needed) + build + run + show the result
#   ./demo.sh build           # just build
#   ./demo.sh run             # just run (requires a prior build)
#
# Builds the default (no-sanitizer) lane, runs the chapter's example binary,
# and prints the shared-lock baseline, the shard-per-core lane, and the
# before/after skew-mitigation lanes. Host-only, unprivileged (see
# CLAUDE.md, "Never change the development host"): no sudo, no package
# install outside this repo's own .toolchain/ and build/ directories. The
# example's own worker threads may best-effort pin themselves to a core via
# ordinary unprivileged sched_setaffinity (see ch08::pin_current_thread_best_effort);
# nothing here touches host configuration.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)" && cd "$SCRIPT_DIR"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
BINARY="$REPO_ROOT/build/examples/08-shard-per-core/cpp/ch08_shard_per_core"
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
  "$REPO_ROOT/scripts/profile.sh" ch08-shard-per-core | \
    "$BINARY" "$RESULTS_DIR" "$REPO_ROOT/scripts/scrub-host-ids.py"
  echo
  echo "--- examples/08-shard-per-core/results/shard-per-core.txt ---"
  grep -E '^(shard_count|shared_lock_(item_count|wall_time_us|throughput_items_per_sec|sample_count|p50_us|p99_us|p999_us)|sharded_(item_count|wall_time_us|throughput_items_per_sec|sample_count|p50_us|p99_us|p999_us|vs_shared_lock_state_mismatch_count)|skew_(item_count|hot_key_count|hot_fraction_percent|target_shard_index)|skew_(before|after)_(hot_shard_index|hot_shard_processed_count|hot_shard_load_percent|max_other_shard_processed_count|hot_vs_max_other_ratio|hot_shard_(sample_count|p50_us|p99_us|p999_us)|other_shards_(sample_count|p50_us|p99_us|p999_us))|skew_after_target_shard_processed_count)=' \
    "$RESULTS_DIR/shard-per-core.txt"
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
