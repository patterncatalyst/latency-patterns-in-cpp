---
title: "Wait-free synchronization"
order: 5
part: "Concurrency and Synchronization"
description: "The scan-to-divert handoff rebuilt from a mutex-guarded queue, where the consumer sleeps and the producer waits on the same lock, into a fixed-capacity ring where neither thread ever blocks, plus the same ring in Rust, where the compiler rejects the race the C++ version depends on a reviewer to catch."
duration: 30 minutes
---

## Learning objectives

By the end of this chapter you can:

- Explain why a mutex-guarded queue's blocking consumer costs more than the
  lock itself: waking a thread parked in a `condition_variable` is a trip
  through the kernel scheduler, paid on every item.
- Build a fixed-capacity single-producer/single-consumer ring whose
  `try_push` and `try_pop` never block, and say what "wait-free" means for
  a data structure precisely (bounded steps per call, not "always
  succeeds").
- Choose `memory_order_acquire`/`memory_order_release` for the ring's head
  and tail indices instead of `memory_order_relaxed`, and say exactly what
  a relaxed ordering would let the hardware or compiler reorder.
- Read a handoff-latency distribution for both queues and say, from the
  numbers, where the blocking design pays its cost.
- Explain what Rust's `Send`/`Sync` traits add to the identical ring that
  C++'s type system cannot: a second producer thread is a compile error,
  not a race a test suite has to get lucky to catch.

## Diagram

{% include excalidraw.html file="wait-free-spsc-handoff" alt="Two panels. Left: a mutex-guarded queue, a scan thread pushing through a lock, a divert thread blocking in a condition_variable wait. Right: a wait-free SPSC ring of six slots, the scan thread writing slot[tail] with a release store, the divert thread reading slot[head] with an acquire load, neither thread blocking." caption="Figure 5.1: Same handoff, two queues. The mutex queue's consumer sleeps between items; the ring's producer and consumer each do one atomic operation and move on." %}

## Why this matters

The previous chapter fixed where a parcel's own fields sit in memory. This
chapter fixes something upstream of memory layout entirely: how the scan
stage hands its decision to the divert stage at all.

A real sortation line's scan station commits an answer, which chute a
parcel is headed for, faster than the diverter can act on it. Something
has to carry that decision from one thread to another: the scan thread
senses and decides, the divert thread acts. The obvious structure is a
queue behind a lock, the shape most code reaches for by default. It works.
It also means every single handoff takes a lock twice, once to push and
once to pop, and when the queue runs dry, the consumer thread stops
running at all until the kernel scheduler wakes it back up. Under light
load that wakeup is cheap and invisible. Under the load this chapter
measures, two threads running flat out with no pacing, it is the whole
story.

The fix is not a faster lock. It is no lock: a fixed-capacity ring where
the producer writes to one end, the consumer reads from the other, and
each side's own atomic index is the only thing the other side ever reads.
Neither `try_push` nor `try_pop` blocks, loops, or asks the OS scheduler
for anything. That is what "wait-free" means here, and it is a sharper
claim than "lock-free": a wait-free operation finishes in a number of
steps bounded in advance, regardless of what any other thread is doing.

The code is in `examples/05-wait-free-sync/`; its `README.md` covers what
it does and how to drive both the C++ and Rust lanes.

## The mutex handoff and where it blocks

`ch05::MutexHandoffQueue` is the textbook producer/consumer queue: one
`std::mutex`, one `std::condition_variable`, one `std::queue<Decision>`
underneath.

```cpp
void MutexHandoffQueue::push(Decision decision) {
    {
        const std::scoped_lock lock(mutex_);
        queue_.push(decision);
    }
    not_empty_.notify_one();
}

bool MutexHandoffQueue::pop(Decision& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) {
        return false;
    }
    out = queue_.front();
    queue_.pop();
    return true;
}
```

Nothing here is wrong. `push` takes the lock, appends, releases, and
notifies outside the lock so a woken consumer does not immediately block
again on a mutex the producer is about to drop anyway. `pop` blocks on the
condition_variable until something is there to take. This is the queue
most codebases already have, because it is correct, general (any number of
producers and consumers), and easy to reason about.

The cost shows up under exactly the load a sortation line's scan stage
produces: items arriving faster than the consumer can sleep and wake back
up for each one. Every `push` and every `pop` compete for the same lock,
and every time the consumer finds the queue empty, it stops running
entirely until `notify_one()` reaches it through the kernel. That wakeup
is not free; it is a futex round trip, and it happens once per item under
sustained load, not once per idle period.

## The wait-free ring and its memory ordering

`ch05::SpscRingQueue<T, Capacity>` replaces the queue with a fixed-size
array and two atomic indices:

```cpp
alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> head_{0};
alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> tail_{0};
std::array<T, Capacity> slots_{};
```

`alignas(std::hardware_destructive_interference_size)` is the same fix the
previous chapter applied to two per-thread counters, aimed here at the
ring's own bookkeeping: `head_` and `tail_` are each owned by a different
thread, and without this they would share a 64-byte line the same way the
unpadded counters in "Zero-copy data layout" did.

```cpp
bool try_push(T value) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t next = advance(tail);
    if (next == head_.load(std::memory_order_acquire)) {
        return false;  // full
    }
    slots_[tail] = value;
    tail_.store(next, std::memory_order_release);
    return true;
}

bool try_pop(T& out) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) {
        return false;  // empty
    }
    out = slots_[head];
    head_.store(advance(head), std::memory_order_release);
    return true;
}
```

Each function touches its own index with `memory_order_relaxed` (no other
thread needs ordering guarantees about a value only this thread ever
writes) and the other side's index with `memory_order_acquire`, paired
against a `memory_order_release` store on that other side. That pairing is
the entire mechanism: a release store makes every write that happened
before it, here, the write to `slots_[tail]`, visible to any thread whose
acquire load observes that store's new value. Without it, `try_push`'s
write to `slots_[tail]` and its update to `tail_` have no ordering
relationship the consumer can rely on. The CPU or the compiler is free to
reorder them, and `memory_order_relaxed` would let a consumer observe the
new `tail_` value before the slot write has actually landed, a live data
race on `slots_[]` that depends on timing to show up at all. Acquire and
release cost nothing on x86-64 (both compile to a plain load and a plain
store; the architecture's own memory model already provides the ordering),
and they are exactly the guarantee this handoff needs: everything the
producer did before publishing an item is visible to whoever reads it.

Capacity is fixed and a power of two, so advancing an index is a mask, not
a modulo, and one slot always stays empty: `head_ == tail_` unambiguously
means empty, never confused with full. A full ring returns `false`
immediately rather than blocking; this chapter's trial retries in a tight
loop when that happens (178 thousand times out of 200,000 pushes, on the
run below, since the ring's consumer runs flat out too but still falls
behind under contention), which is a choice the caller makes, not
something the ring imposes.

## The measurement

`examples/05-wait-free-sync/cpp/` runs 200,000 decisions through each
queue, two threads flat out with no pacing: the producer timestamps a
`Decision` right before handing it off, the consumer timestamps its
receipt, and the difference is the handoff latency. From
`results/wait-free-sync.txt`, measured on a 16-core/22-thread laptop CPU,
powersave governor, bare metal, no pinning or isolation:

| Lane | p50 | p99 | p99.9 |
|---|---|---|---|
| mutex queue (condition_variable, blocking) | 10906.745 us | 16819.617 us | 16926.163 us |
| wait-free ring (atomics, acquire/release) | 714.584 us | 875.431 us | 1004.058 us |

The mutex queue runs about 15.3 times slower at the median, about 19.2
times slower at p99, and about 16.9 times slower at p99.9. The ring wins
at every percentile measured, not only the tail: once the consumer falls
behind under sustained load, each blocked wakeup adds to a backlog that
the next item inherits, so the cost compounds across the run rather than
showing up as a handful of slow outliers. `handoff_mutex_dropped_count`,
`handoff_ring_dropped_count`, and both lanes'
`out_of_order_count`/`duplicate_count` all read `0`: every one of the
200,000 decisions arrived exactly once, in the order it was sent, in both
queues. Swapping the queue changed how long the handoff took, never what
it delivered.

## The C++/Rust side-by-side

The ring itself is the same idea in both languages: a fixed array, two
atomic indices, acquire paired with release.

{% capture cpp_ring %}
```cpp
bool try_push(T value) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t next = advance(tail);
    if (next == head_.load(std::memory_order_acquire)) {
        return false;  // full
    }
    slots_[tail] = value;
    tail_.store(next, std::memory_order_release);
    return true;
}
```
{% endcapture %}
{% capture rust_ring %}
```rust
pub fn try_push(&self, decision: Decision) -> Result<(), Decision> {
    let mask = CAPACITY - 1;
    let tail = self.shared.tail.load(Ordering::Relaxed);
    let next = (tail + 1) & mask;
    if next == self.shared.head.load(Ordering::Acquire) {
        return Err(decision); // full
    }
    unsafe { (*self.shared.slots[tail].get()).write(decision); }
    self.shared.tail.store(next, Ordering::Release);
    Ok(())
}
```
{% endcapture %}
{% include code-tabs.html id="spsc-ring" cpp=cpp_ring rust=rust_ring %}

Same mask, same acquire/release pairing, same one `unsafe` write Rust
requires because the slot lives behind an `UnsafeCell` (Rust's escape
hatch for "I am managing this myself," the same trust C++ extends to every
line by default). Nothing about the ordering logic changed crossing
languages; that part of the lesson is language-neutral.

What is not neutral is who may call `try_push`. The C++ `SpscRingQueue` is
a plain class: nothing in its type stops a second thread from calling
`try_push` on the same instance, which would race the first producer's
write to `slots_[tail]` with no guarantee either one gets a consistent
slot. That misuse compiles, links, and runs; whether it corrupts an item
depends on how the two calls happen to interleave on a given run, under a
given scheduler, on a given machine. TSan catches it if the race fires
during the specific run under test, not as a standing guarantee for every
run after.

Rust's version makes the same misuse a compile error:

{% capture cpp_ownership %}
```cpp
// Nothing in SpscRingQueue's type stops this: a second thread with a
// reference to the same ring can call try_push concurrently with the
// first. It compiles. Whether it corrupts slots_[] depends on timing.
void rogue_second_producer(ch05::SpscRingQueue<Decision, 4096>& ring) {
    ring.try_push(Decision{});  // no compiler objection, ever
}
```
{% endcapture %}
{% capture rust_ownership %}
```rust
// Sender<N> is Send but not Clone and not Sync: there is exactly one
// producer handle per channel, and it cannot be shared behind a
// reference. This is checked by a compile_fail doc test on every
// `cargo test` (see rust/src/lib.rs).
let (tx, _rx) = channel::<16>();
let shared_tx = Arc::new(tx);         // Arc<T> needs T: Sync to be Send
let tx_a = Arc::clone(&shared_tx);
let tx_b = Arc::clone(&shared_tx);
thread::spawn(move || { tx_a.try_push(decision_a); }); // error[E0277]:
thread::spawn(move || { tx_b.try_push(decision_b); }); // Sender<16> is
                                                        // not Sync
```
{% endcapture %}
{% include code-tabs.html id="ownership-guard" cpp=cpp_ownership rust=rust_ownership %}

`Sender<N>` and `Receiver<N>` each wrap the ring's shared state in an
`Arc`, but neither derives `Clone`, so `channel()` is the only place a
`Sender` or a `Receiver` can come from: exactly one of each, ever. Each
also carries a `PhantomData<Cell<()>>` marker field, which is `Send` but
not `Sync`; since an auto trait like `Sync` only holds for a struct when
every field holds it, that one marker field makes `Sender` and `Receiver`
`Send` (movable into the one thread that owns them) but not `Sync`
(unreachable through a shared reference from a second thread). `Arc<T>` is
`Send` only when `T` is both `Send` and `Sync`, so `Arc<Sender<16>>` is not
`Send`, and the attempt above to move it into a second `thread::spawn`
fails before the program exists to race. This is the payoff of the
ownership model this book has mentioned as a sidebar since "Zero-copy data
layout": here, for the first time, it is not a convention a reviewer
enforces by reading the diff. It is a type the compiler checks.

Running the identical wait-free ring through
`examples/05-wait-free-sync/rust/src/bin/handoff_bench.rs`, the same
technique in Rust, not a comparison against the C++ numbers above, over
the same 200,000 decisions: p50 = 389.915 us, p99 = 462.169 us, p99.9 =
465.052 us, with `out_of_order_count` and `duplicate_count` both `0`. The
number is real and it is here for completeness, not for a scoreboard: this
chapter does not claim Rust is faster or slower than C++ at this task, and
the reason to reach for the Rust lane in this book is never speed. It is
that `cargo test` runs a `compile_fail` doctest that tries the two-producer
misuse above and asserts, on every single run, that it does not compile.

## How the code works

**The decision and the two queues.** `ch05::Decision` (`parcel_id`,
`chute`, `enqueued_at`) is the value both queues move. `MutexHandoffQueue`
(`src/mutex_handoff_queue.cpp`) and `SpscRingQueue`
(`include/ch05/spsc_ring_queue.hpp`, header-only) implement the same
push/pop shape with incompatible blocking contracts: one blocks on empty,
the other returns `false`.

**The trial.** `run_mutex_handoff_trial` and `run_ring_handoff_trial`
(`src/handoff_trial.cpp`) each spawn one producer thread and one consumer
thread, run `handoff_item_count` decisions through, and return a
`HandoffTrialResult`: a reserved `std::vector<double>` of per-item
latencies plus the correctness counts this chapter's GoogleTests check
directly (`tests/mutex_handoff_queue_test.cpp`,
`tests/spsc_ring_queue_test.cpp`, `tests/handoff_trial_test.cpp`),
including a real two-thread concurrent run for each queue type.

**The Rust channel.** `ch05_wait_free_sync::channel()`
(`rust/src/lib.rs`) builds one `Arc<Shared<CAPACITY>>` and returns a
`Sender`/`Receiver` pair over it. `Shared` is manually `unsafe impl Sync`,
documented with exactly which single-writer discipline makes that sound;
`Sender`/`Receiver`'s `PhantomData<Cell<()>>` field is what keeps them off
limits to a second thread. `rust/src/bin/handoff_bench.rs` runs the same
200,000-item trial and reports the same shape of result.

## Trade-offs

The wait-free ring trades generality for speed under contention. It is
strictly single-producer/single-consumer and fixed-capacity; a second
producer thread, a growing backlog, or a need to block until space frees
up all need a different structure (a true MPSC queue, a bounded blocking
queue, or backpressure built on top of `try_push`'s `false` return). The
mutex queue stays the right default for a queue with more than one
producer or consumer, or one where occasional sleep-and-wake latency does
not matter. Reach for a wait-free ring specifically where this chapter's
shape holds: exactly two threads, a hot path, and a producer that must
never be made to wait.

The Rust ownership guarantee trades flexibility for the same reason.
`Sender<N>` being unclonable is a feature for this chapter's shape and a
limitation the moment a design needs to hand the producer role to more
than one thread; Rust's own `std::sync::mpsc` exists because that is a
common enough need to deserve a `Clone`-able sender with a different
internal structure. Choosing not to `Clone` here is the specific design
decision that turns "don't do this" into "cannot do this," and it only
makes sense because the chapter's handoff has exactly one producer and one
consumer.

## What you learned

- A mutex-guarded queue's cost under contention is not the lock; it is the
  consumer's wakeup, a kernel scheduler round trip paid once per item.
- A wait-free operation finishes in bounded steps regardless of other
  threads; "the ring was full" is a `false` return, never a wait.
- `memory_order_acquire` paired with `memory_order_release` on the ring's
  head and tail is what makes a slot write visible before the index update
  that announces it; `memory_order_relaxed` would let that ordering
  disappear and turn a correct ring into an intermittent data race.
- The same ring exists safely in C++ and in Rust; what differs is whether
  the single-producer, single-consumer rule is a convention a reviewer
  enforces or a type the compiler checks.
- Neither language lane in this chapter is faster or slower than the
  other by design; the Rust number stands on its own as the same
  technique, never a comparison against the C++ number.

The next chapter keeps two threads on the hot path and changes the
question from how they hand off one decision to how the work itself
splits across more than two.

## For deeper coverage

- Pekka Enberg, *Latency: Reduce Delay in Software Systems* (Manning,
  2025): this guide's reference work for chapter arc and technique
  coverage; see `bibliography.md`.
- Maurice Herlihy and Nir Shavit, *The Art of Multiprocessor Programming*
  (Morgan Kaufmann): the formal definitions of wait-free, lock-free, and
  obstruction-free progress this chapter's ring satisfies the strongest of.
- "Rust Atomics and Locks" by Mara Bos (O'Reilly, 2023): acquire/release
  ordering and `UnsafeCell`-based concurrent structures in Rust, covering
  the same ring shape this chapter builds.

---

*Verification status: <span class="status status--verified">verified</span>.
C++ built with the default CMake/Conan lane; `ctest --preset default`
passes (74 tests total, including this chapter's new `MutexHandoffQueue`,
`SpscRingQueue`, and `HandoffTrial` GoogleTests; every prior chapter's
tests still pass unchanged). The ASan/UBSan lane (`cmake --preset
asan-ubsan`) builds clean and its own `ctest --preset asan-ubsan` passes
(74 tests). Because this chapter is concurrent by definition, the TSan
lane (`cmake --preset tsan`) was built and run both ways required: `ctest
--preset tsan` (74 tests) and the example binary itself run directly under
TSan, with no `ThreadSanitizer` report either time.
`examples/05-wait-free-sync/demo.sh` ran on an unprivileged, untuned
development host and produced `results/wait-free-sync.txt` with the ring
lane faster than the mutex lane at every percentile and every
`dropped_count`/`out_of_order_count`/`duplicate_count` field at `0`; every
number above was copied directly from that file. `python3 -I
scripts/validate.py` passes (host-ids, distribution, harness-cap).

Rust built against the pinned 1.100.0 toolchain. `cargo fmt --check` and
`cargo clippy --all-targets -- -D warnings` on
`examples/05-wait-free-sync/rust/` are both clean. `cargo test` passes 5
unit tests (FIFO order, no loss/duplication, the full-ring boundary, and a
real two-thread concurrent-correctness run) plus the `compile_fail` doc
test on `Sender`, confirmed by relaxing the guard and
watching that same test fail with "compiled successfully, but it's marked
`compile_fail`," then reverting. `cargo build --release` plus
`handoff_bench` produced `results/wait-free-sync-rust.txt` with
`out_of_order_count=0` and `duplicate_count=0`; that number is reported
above as the same technique in Rust, not a comparison against the C++
numbers.*
