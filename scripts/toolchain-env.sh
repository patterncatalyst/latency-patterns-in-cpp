#!/bin/sh
# toolchain-env.sh - print the env vars that select this repo's user-space
# C++ toolchain over whatever the host happens to have on PATH.
#
# This repo pins Clang/LLVM, CMake, and Ninja to exact upstream releases
# unpacked under .toolchain/ by scripts/bootstrap-toolchain.sh (see
# versions.env and _plans/dependency-pins.md for the pins). Nothing here
# touches the host: no dnf, no sudo, no edits outside this repo checkout.
#
# Usage (this script is never sourced directly, so it can resolve its own
# location from $0 without the sourced-script ambiguity):
#   eval "$(scripts/toolchain-env.sh)"
#   cmake --preset default
#
# Output: one `export KEY=value` line per variable. Nothing is printed that
# names this development host (see CLAUDE.md, "Public-repo host-id scrub
# rule"); every path is resolved at run time relative to this script.

set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH='' cd -- "${script_dir}/.." && pwd)
toolchain_dir="${repo_root}/.toolchain"

llvm_dir="${toolchain_dir}/llvm-23.1.6"
cmake_dir="${toolchain_dir}/cmake-4.5.1"
ninja_dir="${toolchain_dir}/ninja-1.13.2"

for d in "$llvm_dir" "$cmake_dir" "$ninja_dir"; do
  if [ ! -d "$d" ]; then
    printf 'toolchain-env.sh: missing %s - run scripts/bootstrap-toolchain.sh first\n' "$d" >&2
    exit 1
  fi
done

printf 'export PATH=%s/bin:%s:%s/bin:$PATH\n' "$llvm_dir" "$ninja_dir" "$cmake_dir"
printf 'export CC=%s/bin/clang\n' "$llvm_dir"
printf 'export CXX=%s/bin/clang++\n' "$llvm_dir"
