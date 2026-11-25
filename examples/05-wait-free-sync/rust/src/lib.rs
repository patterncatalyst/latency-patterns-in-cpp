//! A wait-free single-producer/single-consumer handoff: the same ring as
//! the chapter's C++ `ch05::SpscRingQueue` (same fixed capacity, same
//! acquire/release ordering on the head and tail indices, same
//! never-blocks contract on `try_push`/`try_pop`), in idiomatic Rust.
//!
//! The ring itself is not the lesson; C++ builds the identical ring. The
//! lesson is `Sender<N>` and `Receiver<N>`: neither implements `Clone`, and
//! neither is `Sync`. After [`channel`] returns, there is exactly one
//! producer handle and exactly one consumer handle in the whole program,
//! and neither can be shared behind a reference. A second producer thread
//! racing the first on the same ring, the exact mistake the C++ lane
//! depends on a reviewer (or a TSan run) to catch, does not compile here.
//! See the doc test on [`Sender`] for the failing case, checked by `cargo
//! test` on every run.

use std::array;
use std::cell::{Cell, UnsafeCell};
use std::marker::PhantomData;
use std::mem::MaybeUninit;
use std::ops::Deref;
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::Instant;

/// One scan decision handed from the producer to the consumer: which chute
/// a parcel resolves to, plus the moment it entered the ring. Mirrors
/// `ch05::Decision` in the C++ lane, field for field.
#[derive(Clone, Copy, Debug)]
pub struct Decision {
    pub parcel_id: u64,
    pub chute: i32,
    pub enqueued_at: Instant,
}

/// Forces `T` onto its own cache line, the same role
/// `std::hardware_destructive_interference_size` plays for the C++ ring's
/// `head_`/`tail_` (see "Zero-copy data layout": false sharing is still
/// false sharing whether the two variables are plain counters or ring
/// indices). Stable Rust has no portable query for the real line size, so
/// this fixes 64 bytes, the value every current x86-64 and AArch64 target
/// this book runs on actually uses; `crossbeam-utils::CachePadded` makes
/// the same trade-off for the same reason.
#[repr(align(64))]
struct CachePadded<T>(T);

impl<T> Deref for CachePadded<T> {
    type Target = T;
    fn deref(&self) -> &T {
        &self.0
    }
}

struct Shared<const CAPACITY: usize> {
    head: CachePadded<AtomicUsize>,
    tail: CachePadded<AtomicUsize>,
    slots: [UnsafeCell<MaybeUninit<Decision>>; CAPACITY],
}

// SAFETY: `Shared` is reachable only through one `Sender<CAPACITY>` and one
// `Receiver<CAPACITY>` (see `channel`: neither handle implements `Clone`,
// and neither is `Sync`, so neither can be duplicated or shared behind a
// reference once created). `try_push` is the only code that ever writes
// `tail` or a slot at an index derived from `tail`; `try_pop` is the only
// code that ever writes `head` or reads a slot at an index derived from
// `head`. That single-writer-per-field discipline, plus the acquire/release
// pairing on `head`/`tail` in `try_push`/`try_pop`, is what makes handing
// `Shared` to two threads (one per handle) sound despite the raw
// `UnsafeCell` slots. The compiler cannot see that discipline on its own,
// which is exactly why this impl is unsafe and documented rather than
// automatic.
unsafe impl<const CAPACITY: usize> Sync for Shared<CAPACITY> {}

/// The producer half of the channel. `Send` (move it into the thread that
/// will call [`try_push`](Sender::try_push)) but deliberately not `Sync`:
/// it cannot be reached through a shared reference, so a second thread can
/// never call `try_push` on it concurrently with the first.
///
/// Trying anyway does not compile:
///
/// ```compile_fail
/// # use ch05_wait_free_sync::{channel, Decision};
/// # use std::sync::Arc;
/// # use std::thread;
/// # use std::time::Instant;
/// let (tx, _rx) = channel::<16>();
/// let shared_tx = Arc::new(tx);        // Arc<T> needs T: Sync to be Send
/// let tx_a = Arc::clone(&shared_tx);
/// let tx_b = Arc::clone(&shared_tx);
/// // Two threads, one shared producer handle: exactly the misuse that
/// // would race on the C++ ring's tail_ index at runtime.
/// thread::spawn(move || {
///     let _ = tx_a.try_push(Decision { parcel_id: 1, chute: 0, enqueued_at: Instant::now() });
/// });
/// thread::spawn(move || {
///     let _ = tx_b.try_push(Decision { parcel_id: 2, chute: 0, enqueued_at: Instant::now() });
/// });
/// // error[E0277]: `Cell<()>` cannot be shared between threads safely
/// // (`Sender<16>` is not `Sync`, so `Arc<Sender<16>>` is not `Send`)
/// ```
///
/// The C++ ring has no equivalent guard: `ch05::SpscRingQueue::try_push` is
/// a plain member function, and nothing stops a second thread from calling
/// it on the same ring. That call compiles, links, and runs; whether it
/// corrupts a slot depends on how the two calls happen to interleave, the
/// kind of bug a test suite can miss for months. The Rust version turns
/// the single-producer *assumption* into a single-producer *type*.
pub struct Sender<const CAPACITY: usize> {
    shared: Arc<Shared<CAPACITY>>,
    _not_sync: PhantomData<Cell<()>>,
}

/// The consumer half of the channel. `Send` but not `Sync`, for the same
/// reason as [`Sender`]: it cannot be reached through a shared reference,
/// so a second thread can never call `try_pop` on it concurrently.
pub struct Receiver<const CAPACITY: usize> {
    shared: Arc<Shared<CAPACITY>>,
    _not_sync: PhantomData<Cell<()>>,
}

/// Builds one wait-free ring and returns its producer and consumer handles.
/// `CAPACITY` must be a power of two, at least 2 (checked here rather than
/// at the type level: `where` bounds on const-generic arithmetic are still
/// awkward on stable Rust). One slot stays unused so `head == tail` can
/// mean "empty" without a separate counter; `Sender<CAPACITY>` holds at
/// most `CAPACITY - 1` items at once, matching the C++ ring exactly.
pub fn channel<const CAPACITY: usize>() -> (Sender<CAPACITY>, Receiver<CAPACITY>) {
    assert!(
        CAPACITY >= 2 && CAPACITY.is_power_of_two(),
        "CAPACITY must be a power of two, at least 2"
    );
    let shared = Arc::new(Shared {
        head: CachePadded(AtomicUsize::new(0)),
        tail: CachePadded(AtomicUsize::new(0)),
        slots: array::from_fn(|_| UnsafeCell::new(MaybeUninit::uninit())),
    });
    (
        Sender {
            shared: Arc::clone(&shared),
            _not_sync: PhantomData,
        },
        Receiver {
            shared,
            _not_sync: PhantomData,
        },
    )
}

impl<const CAPACITY: usize> Sender<CAPACITY> {
    /// Never blocks. Returns the decision back on failure rather than
    /// dropping it, so a full ring never silently loses an item; the
    /// caller decides whether to retry immediately (as this chapter's
    /// bench does) or apply backpressure some other way.
    pub fn try_push(&self, decision: Decision) -> Result<(), Decision> {
        let mask = CAPACITY - 1;
        let tail = self.shared.tail.load(Ordering::Relaxed);
        let next = (tail + 1) & mask;
        // Acquire: pairs with the consumer's release store to `head` in
        // try_pop. Seeing the consumer's latest `head` here also makes
        // every slot it has already vacated visible, including the one
        // about to be overwritten. Relaxed would let this load race ahead
        // of the consumer's own read of that slot.
        if next == self.shared.head.load(Ordering::Acquire) {
            return Err(decision); // full
        }
        // SAFETY: only this Sender ever writes slot `tail`, and the
        // acquire load above confirms the consumer has already finished
        // reading it on this pass around the ring.
        unsafe {
            (*self.shared.slots[tail].get()).write(decision);
        }
        // Release: publishes the write above. A consumer that observes
        // this new `tail` with an acquire load is guaranteed to see the
        // slot write too; relaxed here would let the store reorder ahead
        // of the write, and the consumer could read a tail index pointing
        // at a slot whose contents have not landed yet.
        self.shared.tail.store(next, Ordering::Release);
        Ok(())
    }
}

impl<const CAPACITY: usize> Receiver<CAPACITY> {
    /// Never blocks. Returns `None` on an empty ring.
    pub fn try_pop(&self) -> Option<Decision> {
        let mask = CAPACITY - 1;
        let head = self.shared.head.load(Ordering::Relaxed);
        // Acquire: pairs with the producer's release store to `tail`
        // above, the same pairing in the other direction.
        if head == self.shared.tail.load(Ordering::Acquire) {
            return None; // empty
        }
        // SAFETY: the acquire load of `tail` above establishes that the
        // producer's write to slot `head` happened-before this read, and
        // only this Receiver ever reads or retires slot `head`.
        let decision = unsafe { (*self.shared.slots[head].get()).assume_init_read() };
        // Release: publishes "this slot is free again" to the producer's
        // next acquire-load of `head`.
        self.shared.head.store((head + 1) & mask, Ordering::Release);
        Some(decision)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;

    fn decision(parcel_id: u64) -> Decision {
        Decision {
            parcel_id,
            chute: (parcel_id % 16) as i32,
            enqueued_at: Instant::now(),
        }
    }

    #[test]
    fn try_pop_on_empty_ring_returns_none() {
        let (_tx, rx) = channel::<4>();
        assert!(rx.try_pop().is_none());
    }

    #[test]
    fn push_then_pop_returns_the_same_value_in_fifo_order() {
        let (tx, rx) = channel::<4>();
        tx.try_push(decision(10)).unwrap();
        tx.try_push(decision(20)).unwrap();

        assert_eq!(rx.try_pop().unwrap().parcel_id, 10);
        assert_eq!(rx.try_pop().unwrap().parcel_id, 20);
        assert!(rx.try_pop().is_none());
    }

    // A ring of CAPACITY N holds at most N-1 live items: one slot stays
    // empty so the head==tail test can tell "empty" from "full" without a
    // separate counter (see `channel`'s doc comment). This checks that
    // boundary exactly, the same property ch05::SpscRingQueue's C++ test
    // checks for the C++ ring.
    #[test]
    fn full_ring_rejects_one_more_than_its_usable_capacity() {
        let (tx, rx) = channel::<4>();
        tx.try_push(decision(1)).unwrap();
        tx.try_push(decision(2)).unwrap();
        tx.try_push(decision(3)).unwrap();
        assert_eq!(tx.try_push(decision(4)).unwrap_err().parcel_id, 4); // full, item handed back

        assert_eq!(rx.try_pop().unwrap().parcel_id, 1);
        tx.try_push(decision(4)).unwrap(); // room again after one pop
    }

    #[test]
    fn wraps_around_the_physical_array_without_losing_order() {
        let (tx, rx) = channel::<4>();
        for round in 0..10u64 {
            tx.try_push(decision(round)).unwrap();
            assert_eq!(rx.try_pop().unwrap().parcel_id, round);
        }
    }

    // Correctness under real concurrency: one producer thread, one
    // consumer thread, no lost or duplicated items, FIFO order preserved.
    // Matches ch05_wait_free_sync_tests' SpscRingQueue.ConcurrentProducer-
    // ConsumerLosesOrDuplicatesNothing on the C++ side.
    #[test]
    fn concurrent_producer_consumer_loses_or_duplicates_nothing() {
        const ITEM_COUNT: u64 = 5000;
        let (tx, rx) = channel::<256>();

        let producer = thread::spawn(move || {
            for parcel_id in 0..ITEM_COUNT {
                let mut pending = decision(parcel_id);
                while let Err(rejected) = tx.try_push(pending) {
                    pending = rejected;
                }
            }
        });

        let mut received = Vec::with_capacity(ITEM_COUNT as usize);
        while (received.len() as u64) < ITEM_COUNT {
            if let Some(d) = rx.try_pop() {
                received.push(d.parcel_id);
            }
        }
        producer.join().unwrap();

        assert_eq!(received.len(), ITEM_COUNT as usize);
        for (expected, &actual) in received.iter().enumerate() {
            assert_eq!(
                actual, expected as u64,
                "FIFO order broken at position {expected}"
            );
        }
    }
}
