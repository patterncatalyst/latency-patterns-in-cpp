// examples/09-async-processing/rust/src/bin/async_bench.rs
//
// The same technique in Rust, not a comparison against the C++ numbers (see
// CLAUDE.md, "Measurement discipline"): sync-inline versus off-path,
// headline comparison, plus the backpressure demonstration, built against
// the identical `ch09_async_processing` library this crate's tests already
// exercise for correctness. Built in release mode, the way anyone measuring
// a Rust program would; the C++ numbers in the chapter come from this
// book's default, unoptimized Debug preset, so the two sets of absolute
// numbers differ in magnitude for that reason alone, before any question of
// language speed.
//
// Usage: async_bench <results-dir>
//   Reads the host profile header from stdin (see scripts/profile.sh),
//   same contract as every C++ example in this repo.

use ch09_async_processing::{generate_parcels, run_offpath_trial, run_sync_inline_trial};
use std::env;
use std::fs;
use std::io::{self, Read, Write};
use std::path::PathBuf;
use std::time::Duration;

const HEADLINE_ITEM_COUNT: u64 = 20_000;
const BACKPRESSURE_ITEM_COUNT: u64 = 20_000;
const CHUTE_COUNT: u32 = 64;
const BATCH_CAPACITY: usize = 64;

fn percentile(sorted: &[f64], p: f64) -> f64 {
    if sorted.is_empty() {
        return 0.0;
    }
    let rank = ((p / 100.0) * (sorted.len() as f64 - 1.0)).round() as usize;
    sorted[rank.min(sorted.len() - 1)]
}

fn write_distribution(
    out: &mut String,
    prefix: &str,
    completion_us: &[f64],
    shed_count: u64,
    high_water_mark: u64,
) {
    let mut sorted = completion_us.to_vec();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap());
    out.push_str(&format!("{prefix}_sample_count={}\n", sorted.len()));
    out.push_str(&format!("{prefix}_shed_count={shed_count}\n"));
    out.push_str(&format!("{prefix}_high_water_mark={high_water_mark}\n"));
    if sorted.is_empty() {
        return;
    }
    out.push_str(&format!(
        "{prefix}_p50_us={:.3}\n",
        percentile(&sorted, 50.0)
    ));
    out.push_str(&format!(
        "{prefix}_p99_us={:.3}\n",
        percentile(&sorted, 99.0)
    ));
    out.push_str(&format!(
        "{prefix}_p999_us={:.3}\n",
        percentile(&sorted, 99.9)
    ));
}

fn main() {
    let results_dir: PathBuf = env::args()
        .nth(1)
        .expect("usage: async_bench <results-dir>")
        .into();
    fs::create_dir_all(&results_dir).expect("create results dir");
    // .raw/ holds the per-record audit logs: intermediate artifacts this
    // chapter's results/.txt summarizes, not durable results in their own
    // right (see .gitignore's examples/**/results/.raw/ entry).
    fs::create_dir_all(results_dir.join(".raw")).expect("create results/.raw dir");

    let mut profile_header = String::new();
    io::stdin()
        .read_to_string(&mut profile_header)
        .expect("read profile header from stdin");

    let mut out = String::new();
    out.push_str("# Chapter 9 Rust lane: the same off-path async handoff, built on\n");
    out.push_str("# async/await plus the smol executor instead of C++23's event loop\n");
    out.push_str("# plus epoll. Labelled as the same technique in Rust, not a\n");
    out.push_str("# comparison against the C++ numbers (built in release mode, versus\n");
    out.push_str("# the C++ Debug preset).\n");
    out.push_str(&profile_header);

    let headline_parcels = generate_parcels(HEADLINE_ITEM_COUNT, CHUTE_COUNT, 0xA5A5);

    let sync_audit_path = results_dir.join(".raw").join("audit-sync-rust.log");
    let sync_result = run_sync_inline_trial(&headline_parcels, &sync_audit_path);
    out.push_str(
        "\n# --- lane 1: sync-inline (one O_DSYNC write per parcel, on the hot path) ---\n",
    );
    out.push_str(&format!(
        "rust_sync_item_count={}\n",
        sync_result.completion_us.len()
    ));
    out.push_str(&format!(
        "rust_sync_wall_time_us={:.3}\n",
        sync_result.total_wall_time_us
    ));
    out.push_str(&format!(
        "rust_sync_throughput_items_per_sec={:.1}\n",
        headline_parcels.len() as f64 / (sync_result.total_wall_time_us * 1e-6)
    ));
    write_distribution(&mut out, "rust_sync", &sync_result.completion_us, 0, 0);

    let offpath_audit_path = results_dir.join(".raw").join("audit-offpath-rust.log");
    let offpath_result = run_offpath_trial(
        &headline_parcels,
        &offpath_audit_path,
        4096,
        BATCH_CAPACITY,
        Duration::from_millis(5),
        Duration::from_micros(5_000),
    );
    out.push_str("\n# --- lane 2: off-path (queue capacity 4096, batch 64, max_wait 5000us) ---\n");
    out.push_str(&format!(
        "rust_offpath_item_count={}\n",
        offpath_result.completion_us.len()
    ));
    out.push_str(&format!(
        "rust_offpath_wall_time_us={:.3}\n",
        offpath_result.total_wall_time_us
    ));
    out.push_str(&format!(
        "rust_offpath_throughput_items_per_sec={:.1}\n",
        headline_parcels.len() as f64 / (offpath_result.total_wall_time_us * 1e-6)
    ));
    write_distribution(
        &mut out,
        "rust_offpath",
        &offpath_result.completion_us,
        offpath_result.shed_count,
        offpath_result.high_water_mark as u64,
    );

    let backpressure_parcels = generate_parcels(BACKPRESSURE_ITEM_COUNT, CHUTE_COUNT, 0xB5B5);
    let backpressure_audit_path = results_dir.join(".raw").join("audit-backpressure-rust.log");
    let backpressure_result = run_offpath_trial(
        &backpressure_parcels,
        &backpressure_audit_path,
        64,
        BATCH_CAPACITY,
        Duration::from_millis(5),
        Duration::from_micros(50),
    );
    out.push_str("\n# --- lane 3: backpressure demo (queue capacity 64, max_wait 50us) ---\n");
    out.push_str(&format!(
        "rust_backpressure_item_count={}\n",
        backpressure_result.completion_us.len()
    ));
    out.push_str(&format!(
        "rust_backpressure_wall_time_us={:.3}\n",
        backpressure_result.total_wall_time_us
    ));
    write_distribution(
        &mut out,
        "rust_backpressure",
        &backpressure_result.completion_us,
        backpressure_result.shed_count,
        backpressure_result.high_water_mark as u64,
    );

    let results_path = results_dir.join("async-processing-rust.txt");
    fs::File::create(&results_path)
        .and_then(|mut f| f.write_all(out.as_bytes()))
        .expect("write results file");

    println!(
        "sync:         wall={:.3}ms",
        sync_result.total_wall_time_us / 1000.0
    );
    println!(
        "off-path:     wall={:.3}ms shed={}",
        offpath_result.total_wall_time_us / 1000.0,
        offpath_result.shed_count
    );
    println!(
        "backpressure: shed={} of {}",
        backpressure_result.shed_count,
        backpressure_parcels.len()
    );
    println!("results written to {}", results_path.display());
}
