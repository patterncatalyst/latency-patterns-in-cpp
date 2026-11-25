//! Toolchain probe for the hermetic, pinned Rust lane (see
//! `rust-toolchain.toml`, `scripts/bootstrap-rust.sh`, `scripts/rust-env.sh`).
//!
//! This crate is throwaway scaffolding, not chapter content. It proves that
//! `cargo build`, `cargo test`, `cargo clippy -- -D warnings`, and
//! `cargo fmt --check` all pass clean against the pinned toolchain before
//! the first real Rust lane lands. A later chapter replaces or removes it.

/// Adds two values. Trivial on purpose: this function exists only to give
/// the probe crate something to build, lint, and test.
pub fn add(left: u32, right: u32) -> u32 {
    left + right
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn add_sums_two_values() {
        assert_eq!(add(2, 3), 5);
    }
}
