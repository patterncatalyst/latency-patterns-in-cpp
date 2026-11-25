// examples/05-wait-free-sync/rust/src/bin/handoff_bench.rs
//
// Runs the identical wait-free SPSC handoff as the chapter's C++ lane,
// through the Rust channel() in ch05_wait_free_sync::lib. This measures
// the same technique in Rust; it is not a cross-language comparison (see
// CLAUDE.md, "Do not add a Rust lane outside Ch5, Ch6, Ch9" and the
// chapter prose's "The Rust lane: the same technique, a different
// guarantee"). Reads a profile header from stdin (the same convention
// examples/_lib/include/measure/scrub.hpp's C++ binaries use, so
// scripts/profile.sh pipes into either one the same way) and writes a
// results block to stdout for the caller to redirect and scrub.
use ch05_wait_free_sync::{Decision, channel};
use std::io::{self, Read, Write};
use std::thread;
use std::time::Instant;

const ITEM_COUNT: u64 = 200_000;
const RING_CAPACITY: usize = 4096;

fn percentile(sorted_us: &[f64], p: f64) -> f64 {
    assert!(!sorted_us.is_empty(), "percentile of an empty sample set");
    let rank = ((p / 100.0) * sorted_us.len() as f64).ceil() as usize;
    let index = rank.saturating_sub(1).min(sorted_us.len() - 1);
    sorted_us[index]
}

fn main() {
    let mut profile_header = String::new();
    io::stdin()
        .read_to_string(&mut profile_header)
        .expect("read the profile header from stdin (see scripts/profile.sh)");

    let (tx, rx) = channel::<RING_CAPACITY>();

    let producer = thread::spawn(move || {
        for parcel_id in 0..ITEM_COUNT {
            let decision = Decision {
                parcel_id,
                chute: (parcel_id % 16) as i32,
                enqueued_at: Instant::now(),
            };
            let mut pending = decision;
            while let Err(rejected) = tx.try_push(pending) {
                pending = rejected; // ring full; retry is the caller's choice, not a block
            }
        }
    });

    let mut latencies_us = Vec::with_capacity(ITEM_COUNT as usize);
    let mut received = Vec::with_capacity(ITEM_COUNT as usize);
    while (received.len() as u64) < ITEM_COUNT {
        if let Some(decision) = rx.try_pop() {
            latencies_us.push(decision.enqueued_at.elapsed().as_secs_f64() * 1e6);
            received.push(decision.parcel_id);
        }
    }
    producer.join().expect("producer thread panicked");

    let mut out_of_order_count = 0u64;
    for (expected, &actual) in received.iter().enumerate() {
        if actual != expected as u64 {
            out_of_order_count += 1;
        }
    }
    let mut seen = vec![false; ITEM_COUNT as usize];
    let mut duplicate_count = 0u64;
    for &parcel_id in &received {
        if seen[parcel_id as usize] {
            duplicate_count += 1;
        }
        seen[parcel_id as usize] = true;
    }

    let mut sorted_us = latencies_us;
    sorted_us.sort_by(|a, b| a.partial_cmp(b).expect("latency sample was NaN"));

    let stdout = io::stdout();
    let mut out = stdout.lock();
    writeln!(
        out,
        "# Chapter 5 wait-free-sync Rust lane: the same wait-free SPSC"
    )
    .unwrap();
    writeln!(
        out,
        "# handoff as examples/05-wait-free-sync/cpp, through the Rust"
    )
    .unwrap();
    writeln!(
        out,
        "# channel() in ch05_wait_free_sync. This is the same technique in"
    )
    .unwrap();
    writeln!(
        out,
        "# Rust, not a comparison against the C++ numbers above."
    )
    .unwrap();
    write!(out, "{profile_header}").unwrap();
    writeln!(out, "handoff_item_count={ITEM_COUNT}").unwrap();
    writeln!(out, "ring_capacity={RING_CAPACITY}").unwrap();
    writeln!(out, "handoff_rust_ring_items_sent={ITEM_COUNT}").unwrap();
    writeln!(out, "handoff_rust_ring_items_received={}", received.len()).unwrap();
    writeln!(
        out,
        "handoff_rust_ring_out_of_order_count={out_of_order_count}"
    )
    .unwrap();
    writeln!(out, "handoff_rust_ring_duplicate_count={duplicate_count}").unwrap();
    writeln!(out, "handoff_rust_ring_sample_count={}", sorted_us.len()).unwrap();
    writeln!(
        out,
        "handoff_rust_ring_p50_us={:.3}",
        percentile(&sorted_us, 50.0)
    )
    .unwrap();
    writeln!(
        out,
        "handoff_rust_ring_p99_us={:.3}",
        percentile(&sorted_us, 99.0)
    )
    .unwrap();
    writeln!(
        out,
        "handoff_rust_ring_p999_us={:.3}",
        percentile(&sorted_us, 99.9)
    )
    .unwrap();
}
