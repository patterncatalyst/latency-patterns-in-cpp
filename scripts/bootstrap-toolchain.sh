#!/bin/sh
# bootstrap-toolchain.sh - fetch the pinned, user-space C++ toolchain.
#
# This repo never builds the measured examples with a Fedora-packaged
# compiler (see CLAUDE.md, "Dependencies": every pin is exact and upstream).
# This script downloads the exact upstream releases named in versions.env
# into .toolchain/ (gitignored, never committed) and verifies each archive's
# sha256 before extracting. It never touches anything outside this repo
# checkout, never requires sudo, and never installs an RPM.
#
# Usage: scripts/bootstrap-toolchain.sh
# Idempotent: skips a component whose expected directory already exists.

set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH='' cd -- "${script_dir}/.." && pwd)
toolchain_dir="${repo_root}/.toolchain"

llvm_version="23.1.6"
llvm_sha256="bd4d1c434ff69ef06d19e7934c04708da677e5f2eb51f5bcf052ab91818f0094"
llvm_url="https://github.com/llvm/llvm-project/releases/download/llvmorg-${llvm_version}/LLVM-${llvm_version}-Linux-X64.tar.xz"

cmake_version="4.5.1"
cmake_sha256="83b71181fb62b4fe8f26067a9321448572e7a43bd04ff6c7f54a24c33698f16d"
cmake_url="https://github.com/Kitware/CMake/releases/download/v${cmake_version}/cmake-${cmake_version}-linux-x86_64.tar.gz"

ninja_version="1.13.2"
ninja_sha256="5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6"
ninja_url="https://github.com/ninja-build/ninja/releases/download/v${ninja_version}/ninja-linux.zip"

download_dir=$(mktemp -d)
trap 'rm -rf "$download_dir"' EXIT

verify_sha256() {
  file_path=$1
  expected=$2
  actual=$(sha256sum "$file_path" | awk '{print $1}')
  if [ "$actual" != "$expected" ]; then
    printf 'bootstrap-toolchain.sh: checksum mismatch for %s\n  expected %s\n  got      %s\n' \
      "$file_path" "$expected" "$actual" >&2
    exit 1
  fi
}

mkdir -p "$toolchain_dir"

# --- Clang/LLVM -------------------------------------------------------------
if [ -x "${toolchain_dir}/llvm-${llvm_version}/bin/clang++" ]; then
  echo "llvm-${llvm_version}: already present, skipping"
else
  echo "llvm-${llvm_version}: downloading (~2.0 GB)..."
  archive="${download_dir}/llvm.tar.xz"
  curl -sS -L -o "$archive" "$llvm_url"
  verify_sha256 "$archive" "$llvm_sha256"
  tar -xJf "$archive" -C "$toolchain_dir"
  mv "${toolchain_dir}/LLVM-${llvm_version}-Linux-X64" "${toolchain_dir}/llvm-${llvm_version}"
fi

# --- CMake --------------------------------------------------------------
if [ -x "${toolchain_dir}/cmake-${cmake_version}/bin/cmake" ]; then
  echo "cmake-${cmake_version}: already present, skipping"
else
  echo "cmake-${cmake_version}: downloading..."
  archive="${download_dir}/cmake.tar.gz"
  curl -sS -L -o "$archive" "$cmake_url"
  verify_sha256 "$archive" "$cmake_sha256"
  tar -xzf "$archive" -C "$toolchain_dir"
  mv "${toolchain_dir}/cmake-${cmake_version}-linux-x86_64" "${toolchain_dir}/cmake-${cmake_version}"
fi

# --- Ninja ----------------------------------------------------------------
if [ -x "${toolchain_dir}/ninja-${ninja_version}/ninja" ]; then
  echo "ninja-${ninja_version}: already present, skipping"
else
  echo "ninja-${ninja_version}: downloading..."
  archive="${download_dir}/ninja.zip"
  curl -sS -L -o "$archive" "$ninja_url"
  verify_sha256 "$archive" "$ninja_sha256"
  mkdir -p "${toolchain_dir}/ninja-${ninja_version}"
  unzip -q -o "$archive" -d "${toolchain_dir}/ninja-${ninja_version}"
  chmod +x "${toolchain_dir}/ninja-${ninja_version}/ninja"
fi

echo "Toolchain ready under ${toolchain_dir}. Run: eval \"\$(scripts/toolchain-env.sh)\""
