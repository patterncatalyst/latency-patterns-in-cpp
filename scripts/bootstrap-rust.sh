#!/bin/sh
# bootstrap-rust.sh - fetch the pinned, user-space Rust toolchain via rustup.
#
# Mirrors bootstrap-toolchain.sh's approach for the C++ toolchain: the Rust
# lane (Ch5, Ch6, Ch9) never builds with the host's own rustup/cargo (see
# CLAUDE.md, "Dependencies": every pin is exact and upstream). This script
# installs rustup itself into a repo-local, gitignored .toolchain/rust/
# directory with its own RUSTUP_HOME and CARGO_HOME, never the host's
# ~/.rustup or ~/.cargo, then installs the exact pinned stable release named
# below and in rust-toolchain.toml / versions.env, plus the rustfmt and
# clippy components. It never touches anything outside this repo checkout,
# never requires sudo, and never installs a system package.
#
# Usage: scripts/bootstrap-rust.sh
# Idempotent: skips the rustup-init download if the repo-local rustup binary
# already exists; `rustup toolchain install` itself skips any toolchain or
# component already present without touching the network.

set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH='' cd -- "${script_dir}/.." && pwd)
rust_toolchain_dir="${repo_root}/.toolchain/rust"
RUSTUP_HOME="${rust_toolchain_dir}/rustup"
CARGO_HOME="${rust_toolchain_dir}/cargo"
export RUSTUP_HOME CARGO_HOME

rust_version="1.100.0"
target_triple="x86_64-unknown-linux-gnu"
rustup_init_sha256="b1c8cb14e30889e7e2999e6b69e62d9e9b9ffc0f4df8180564d306d9680e6c55"
rustup_init_url="https://static.rust-lang.org/rustup/dist/${target_triple}/rustup-init"

download_dir=$(mktemp -d)
trap 'rm -rf "$download_dir"' EXIT

verify_sha256() {
  file_path=$1
  expected=$2
  actual=$(sha256sum "$file_path" | awk '{print $1}')
  if [ "$actual" != "$expected" ]; then
    printf 'bootstrap-rust.sh: checksum mismatch for %s\n  expected %s\n  got      %s\n' \
      "$file_path" "$expected" "$actual" >&2
    exit 1
  fi
}

mkdir -p "$rust_toolchain_dir"

# --- rustup itself, installed into the repo-local RUSTUP_HOME/CARGO_HOME ----
if [ -x "${CARGO_HOME}/bin/rustup" ]; then
  echo "rustup: already present under ${CARGO_HOME}, skipping install"
else
  echo "rustup: downloading rustup-init for ${target_triple}..."
  installer="${download_dir}/rustup-init"
  curl -sS -L -o "$installer" "$rustup_init_url"
  verify_sha256 "$installer" "$rustup_init_sha256"
  chmod +x "$installer"
  "$installer" -y --no-modify-path --default-toolchain none --profile minimal -q
fi

rustup_bin="${CARGO_HOME}/bin/rustup"

# --- the pinned stable toolchain, plus rustfmt and clippy -------------------
# `rustup toolchain install` is itself idempotent: an already-installed
# toolchain or component is detected and skipped without a network call.
echo "rust-${rust_version}: ensuring toolchain + rustfmt + clippy..."
"$rustup_bin" toolchain install "$rust_version" --profile minimal \
  --component rustfmt --component clippy

echo "Rust toolchain ready under ${rust_toolchain_dir}. Run: eval \"\$(scripts/rust-env.sh)\""
