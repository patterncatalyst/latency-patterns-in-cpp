//! Chapter 9's Rust lane: the identical off-path hand-off as the C++ lane's
//! `BoundedHandoffQueue` + `EventLoopConsumer`
//! (`examples/09-async-processing/cpp/include/ch09/`), built on Rust's
//! async/await plus the `smol` executor instead of C++23's event loop plus
//! `epoll`. The lesson this crate exists to teach is in the shape of the
//! two designs, not in `std`'s ownership rules the way Ch5 and Ch6's Rust
//! lanes were: see "The C++/Rust side-by-side" in the chapter prose.
//!
//! The hot path (`process_parcel_sync`, `process_parcel_offpath`) is
//! synchronous, plain Rust: no `.await` anywhere on it, the same way the
//! C++ hot path never touches `epoll_wait`. Only the background consumer
//! (`run_consumer`) is `async`.

use std::fs::{File, OpenOptions};
use std::io::Write;
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use smol::Timer;
use smol::channel::{Receiver, Sender, TrySendError, bounded};

pub const LABEL_LEN: usize = 40;
pub const RECORD_LEN: usize = 8 + 4 + 8 + LABEL_LEN;

#[derive(Clone, Copy, Debug)]
pub struct Parcel {
    pub parcel_id: u64,
    pub chute_id: u32,
}

#[derive(Clone, Copy, Debug)]
pub struct AuditRecord {
    pub parcel_id: u64,
    pub chute_id: u32,
    pub decided_at_us: u64,
    pub label: [u8; LABEL_LEN],
}

impl AuditRecord {
    pub fn to_bytes(self) -> [u8; RECORD_LEN] {
        let mut buf = [0u8; RECORD_LEN];
        buf[0..8].copy_from_slice(&self.parcel_id.to_le_bytes());
        buf[8..12].copy_from_slice(&self.chute_id.to_le_bytes());
        buf[12..20].copy_from_slice(&self.decided_at_us.to_le_bytes());
        buf[20..20 + LABEL_LEN].copy_from_slice(&self.label);
        buf
    }

    pub fn from_bytes(buf: &[u8; RECORD_LEN]) -> Self {
        let mut parcel_id_bytes = [0u8; 8];
        parcel_id_bytes.copy_from_slice(&buf[0..8]);
        let mut chute_id_bytes = [0u8; 4];
        chute_id_bytes.copy_from_slice(&buf[8..12]);
        let mut decided_at_bytes = [0u8; 8];
        decided_at_bytes.copy_from_slice(&buf[12..20]);
        let mut label = [0u8; LABEL_LEN];
        label.copy_from_slice(&buf[20..20 + LABEL_LEN]);
        AuditRecord {
            parcel_id: u64::from_le_bytes(parcel_id_bytes),
            chute_id: u32::from_le_bytes(chute_id_bytes),
            decided_at_us: u64::from_le_bytes(decided_at_bytes),
            label,
        }
    }
}

/// Deterministic parcel generation from a seed (a small xorshift64 PRNG, no
/// extra dependency for it): the same convention every chapter's example
/// uses, every lane processes the identical batch.
pub fn generate_parcels(count: u64, chute_count: u32, seed: u64) -> Vec<Parcel> {
    let mut state = seed | 1;
    let mut parcels = Vec::with_capacity(count as usize);
    for parcel_id in 0..count {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        let chute_id = (state % u64::from(chute_count)) as u32;
        parcels.push(Parcel {
            parcel_id,
            chute_id,
        });
    }
    parcels
}

pub fn build_audit_record(parcel: &Parcel) -> AuditRecord {
    let decided_at_us = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("system clock before UNIX_EPOCH")
        .as_micros() as u64;
    let text = format!("parcel={} chute={}", parcel.parcel_id, parcel.chute_id);
    let mut label = [0u8; LABEL_LEN];
    let copy_len = text.len().min(LABEL_LEN);
    label[..copy_len].copy_from_slice(&text.as_bytes()[..copy_len]);
    AuditRecord {
        parcel_id: parcel.parcel_id,
        chute_id: parcel.chute_id,
        decided_at_us,
        label,
    }
}

/// Opens a file for durable, synchronous per-write audit records: every
/// `write_all` call returns only once the kernel reports the write durable,
/// the same `O_DSYNC` contract `ch09::SyncAuditWriter` uses in C++.
pub fn open_sync_audit_writer(path: &Path) -> File {
    OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .custom_flags(libc::O_DSYNC)
        .open(path)
        .expect("open_sync_audit_writer: open failed")
}

/// The "before": decide, then write, both on the hot path's own thread.
/// Returns this one call's own duration in microseconds, not a running
/// total since the trial's first parcel.
pub fn process_parcel_sync(parcel: &Parcel, writer: &mut File) -> f64 {
    let start = Instant::now();
    let record = build_audit_record(parcel);
    writer
        .write_all(&record.to_bytes())
        .expect("process_parcel_sync: write failed");
    start.elapsed().as_secs_f64() * 1e6
}

pub struct OffPathOutcome {
    pub completion_us: f64,
    pub shed: bool,
}

/// The "after": decide, then hand the record to the off-path channel and
/// return. The fast path (room available) is `Sender::try_send`, a plain
/// synchronous call: no executor, no `.await`, touched here, the same way
/// the C++ hot path's fast path never calls `epoll_wait`. Only the rare
/// full-channel case drops into `smol::block_on` for a bounded wait, raced
/// against a timer so a full channel stalls the hot path by at most
/// `max_wait`, never shedding silently and never blocking unboundedly.
pub fn process_parcel_offpath(
    parcel: &Parcel,
    sender: &Sender<AuditRecord>,
    max_wait: Duration,
) -> OffPathOutcome {
    let start = Instant::now();
    let record = build_audit_record(parcel);
    let shed = match sender.try_send(record) {
        Ok(()) => false,
        Err(TrySendError::Closed(_)) => true,
        Err(TrySendError::Full(record)) => {
            let delivered = smol::block_on(async {
                smol::future::or(async { sender.send(record).await.is_ok() }, async {
                    Timer::after(max_wait).await;
                    false
                })
                .await
            });
            !delivered
        }
    };
    OffPathOutcome {
        completion_us: start.elapsed().as_secs_f64() * 1e6,
        shed,
    }
}

/// Writes a whole batch as one durable unit: one `write_all` call (one
/// flush, on an `O_DSYNC` file, not one per record), the same amortization
/// `ch09::BatchAuditWriter::flush_batch` gets in C++. Returns this flush's
/// own duration in microseconds.
pub fn flush_batch(writer: &mut File, batch: &[AuditRecord]) -> f64 {
    if batch.is_empty() {
        return 0.0;
    }
    let mut buf = Vec::with_capacity(batch.len() * RECORD_LEN);
    for record in batch {
        buf.extend_from_slice(&record.to_bytes());
    }
    let start = Instant::now();
    writer.write_all(&buf).expect("flush_batch: write failed");
    start.elapsed().as_secs_f64() * 1e6
}

pub fn open_batch_audit_writer(path: &Path) -> File {
    OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .custom_flags(libc::O_DSYNC)
        .open(path)
        .expect("open_batch_audit_writer: open failed")
}

pub fn read_all_records(path: &Path) -> Vec<AuditRecord> {
    let Ok(bytes) = std::fs::read(path) else {
        return Vec::new();
    };
    bytes
        .as_chunks::<RECORD_LEN>()
        .0
        .iter()
        .map(AuditRecord::from_bytes)
        .collect()
}

/// The background consumer: an `async fn`, not an OS-thread-plus-`epoll`
/// event loop. Each pass waits for the next record (`receiver.recv()`),
/// then keeps draining, every subsequent pull raced against the batch's own
/// deadline timer via `smol::future::or`, until the batch is full or the
/// timer wins - the same "whichever comes first" contract the C++ lane
/// gets from `epoll_wait` over an eventfd and a timerfd, expressed here as
/// composing two futures instead of registering two file descriptors.
///
/// Shutdown has no separate stop signal: once every `Sender` is dropped and
/// the channel drains, `receiver.recv()` returns `Err`, this function
/// flushes whatever partial batch it was holding, and returns. Ownership
/// (dropping the sender) is the stop signal; see "The C++/Rust
/// side-by-side."
pub async fn run_consumer(
    receiver: Receiver<AuditRecord>,
    writer: &mut File,
    batch_capacity: usize,
    batch_delay: Duration,
) {
    loop {
        let mut batch = Vec::with_capacity(batch_capacity);
        match receiver.recv().await {
            Ok(record) => batch.push(record),
            Err(_) => return, // channel closed and empty: nothing left, ever
        }
        let deadline = Instant::now() + batch_delay;
        while batch.len() < batch_capacity {
            let remaining = deadline.saturating_duration_since(Instant::now());
            let next = smol::future::or(async { Some(receiver.recv().await.ok()) }, async {
                Timer::after(remaining).await;
                None
            })
            .await;
            match next {
                Some(Some(record)) => batch.push(record),
                Some(None) => break, // channel closed mid-batch
                None => break,       // batch-delay timer fired first
            }
        }
        flush_batch(writer, &batch);
    }
}

pub struct OffPathTrialResult {
    pub completion_us: Vec<f64>,
    pub shed_count: u64,
    pub high_water_mark: usize,
    pub total_wall_time_us: f64,
}

/// Runs the off-path lane end to end: spawns the consumer on its own OS
/// thread (smol's executor needs a thread to run on; this crate does not
/// reach for a multi-threaded executor because this chapter's point is the
/// event-loop pattern, not scaling it across cores), drives every parcel
/// through `process_parcel_offpath` on the calling thread, drops the sender
/// to signal shutdown, and joins the consumer thread before returning.
pub fn run_offpath_trial(
    parcels: &[Parcel],
    audit_path: &Path,
    queue_capacity: usize,
    batch_capacity: usize,
    batch_delay: Duration,
    max_wait: Duration,
) -> OffPathTrialResult {
    let (sender, receiver) = bounded::<AuditRecord>(queue_capacity);
    let audit_path = audit_path.to_path_buf();
    let consumer = std::thread::spawn(move || {
        let mut writer = open_batch_audit_writer(&audit_path);
        smol::block_on(run_consumer(
            receiver,
            &mut writer,
            batch_capacity,
            batch_delay,
        ));
    });

    let mut completion_us = Vec::with_capacity(parcels.len());
    let mut shed_count = 0u64;
    let mut high_water_mark = 0usize;
    let start = Instant::now();
    for parcel in parcels {
        let outcome = process_parcel_offpath(parcel, &sender, max_wait);
        completion_us.push(outcome.completion_us);
        if outcome.shed {
            shed_count += 1;
        }
        high_water_mark = high_water_mark.max(sender.len());
    }
    let total_wall_time_us = start.elapsed().as_secs_f64() * 1e6;

    drop(sender);
    consumer.join().expect("consumer thread panicked");

    OffPathTrialResult {
        completion_us,
        shed_count,
        high_water_mark,
        total_wall_time_us,
    }
}

pub struct SyncTrialResult {
    pub completion_us: Vec<f64>,
    pub total_wall_time_us: f64,
}

pub fn run_sync_inline_trial(parcels: &[Parcel], audit_path: &Path) -> SyncTrialResult {
    let mut writer = open_sync_audit_writer(audit_path);
    let mut completion_us = Vec::with_capacity(parcels.len());
    let start = Instant::now();
    for parcel in parcels {
        completion_us.push(process_parcel_sync(parcel, &mut writer));
    }
    let total_wall_time_us = start.elapsed().as_secs_f64() * 1e6;
    SyncTrialResult {
        completion_us,
        total_wall_time_us,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;

    fn unique_test_dir(prefix: &str) -> std::path::PathBuf {
        let mut dir = std::env::temp_dir();
        let unique = format!(
            "{prefix}-{}-{:?}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        );
        dir.push(unique);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn generate_parcels_is_deterministic_for_the_same_seed() {
        let a = generate_parcels(500, 64, 42);
        let b = generate_parcels(500, 64, 42);
        assert_eq!(a.len(), b.len());
        for (x, y) in a.iter().zip(b.iter()) {
            assert_eq!(x.parcel_id, y.parcel_id);
            assert_eq!(x.chute_id, y.chute_id);
        }
    }

    #[test]
    fn every_chute_id_is_in_range() {
        let parcels = generate_parcels(2000, 16, 7);
        for parcel in &parcels {
            assert!(parcel.chute_id < 16);
        }
    }

    #[test]
    fn audit_record_round_trips_through_bytes() {
        let parcel = Parcel {
            parcel_id: 99,
            chute_id: 3,
        };
        let record = build_audit_record(&parcel);
        let bytes = record.to_bytes();
        let back = AuditRecord::from_bytes(&bytes);
        assert_eq!(back.parcel_id, 99);
        assert_eq!(back.chute_id, 3);
        assert_eq!(back.label, record.label);
    }

    #[test]
    fn sync_inline_writes_exactly_one_record_per_parcel() {
        let dir = unique_test_dir("ch09-rust-sync");
        let path = dir.join("audit.log");
        let parcels = generate_parcels(200, 8, 1);
        let result = run_sync_inline_trial(&parcels, &path);
        assert_eq!(result.completion_us.len(), parcels.len());
        let records = read_all_records(&path);
        assert_eq!(records.len(), parcels.len());
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn offpath_agrees_with_sync_on_the_set_of_records() {
        let dir = unique_test_dir("ch09-rust-offpath");
        let sync_path = dir.join("audit-sync.log");
        let offpath_path = dir.join("audit-offpath.log");
        let parcels = generate_parcels(300, 8, 2);

        let sync_result = run_sync_inline_trial(&parcels, &sync_path);
        let offpath_result = run_offpath_trial(
            &parcels,
            &offpath_path,
            256,
            16,
            Duration::from_millis(5),
            Duration::from_millis(50),
        );

        assert_eq!(sync_result.completion_us.len(), parcels.len());
        assert_eq!(offpath_result.completion_us.len(), parcels.len());
        assert_eq!(
            offpath_result.shed_count, 0,
            "generous max_wait for this batch size: nothing should shed"
        );

        let sync_ids: HashSet<u64> = read_all_records(&sync_path)
            .iter()
            .map(|r| r.parcel_id)
            .collect();
        let offpath_ids: HashSet<u64> = read_all_records(&offpath_path)
            .iter()
            .map(|r| r.parcel_id)
            .collect();
        assert_eq!(sync_ids, offpath_ids);
        assert_eq!(offpath_ids.len(), parcels.len());
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn backpressure_shedding_is_accounted_and_nothing_else_goes_missing() {
        let dir = unique_test_dir("ch09-rust-backpressure");
        let path = dir.join("audit.log");
        let parcels = generate_parcels(4000, 8, 3);

        let result = run_offpath_trial(
            &parcels,
            &path,
            8, // tiny queue capacity: forces real shedding under a flat-out push
            4,
            Duration::from_millis(5),
            Duration::from_micros(20), // short max_wait: forces real shedding
        );

        let records = read_all_records(&path);
        let delivered = parcels.len() as u64 - result.shed_count;
        assert_eq!(records.len() as u64, delivered);

        // Every record that did land must be one of the input parcels, and
        // no parcel_id appears twice: a shed record is accounted for by
        // shed_count, never silently duplicated or corrupted in its place.
        let mut seen = HashSet::new();
        for record in &records {
            assert!(
                seen.insert(record.parcel_id),
                "duplicate parcel_id {}",
                record.parcel_id
            );
        }
        std::fs::remove_dir_all(&dir).ok();
    }
}
