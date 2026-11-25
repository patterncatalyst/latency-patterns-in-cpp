#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <new>

namespace ch05 {

// The "after" handoff: a fixed-capacity, single-producer/single-consumer
// ring buffer. One thread calls try_push(), a different thread calls
// try_pop(), and neither call ever blocks: each returns immediately,
// successful or not, in a number of steps bounded regardless of what the
// other thread is doing. That bounded-steps-per-call property is what
// "wait-free" means here; it is not a claim that an item always gets
// through on the first try; a caller that needs delivery retries, the ring
// itself never waits on anything.
//
// Capacity must be a power of two so the index wrap is a mask, not a
// modulo. `head_` and `tail_` each get their own cache line via
// `alignas(std::hardware_destructive_interference_size)`, so the producer
// writing `tail_` and the consumer writing `head_` never invalidate each
// other's cache line the way the previous chapter's unpadded counters did
// ("Zero-copy data layout" — false sharing is still false sharing whether
// the two variables are plain counters or the indices of a ring).
template <typename T, std::size_t Capacity>
class SpscRingQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two, at least 2");

public:
    // Producer-only. Returns false if the ring is full (the consumer has
    // not yet caught up); the caller decides whether to retry or drop.
    // Never blocks, never loops internally.
    bool try_push(T value) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = advance(tail);
        // Acquire: if this load observes the consumer's most recent
        // head_.store(..., release), every slot the consumer has already
        // vacated (including the one about to be overwritten) is also
        // visible here. Relaxed would let this load reorder ahead of the
        // consumer's read of slots_[head] in program order on the reading
        // side's perspective, risking an overwrite the consumer has not
        // actually finished reading yet.
        if (next == head_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        slots_[tail] = value;
        // Release: publishes the write to slots_[tail] above. A consumer
        // that loads this new tail_ value with acquire is guaranteed to see
        // that write too; memory_order_relaxed here would let the compiler
        // or CPU reorder the tail_ store ahead of the slot write, and the
        // consumer could then read a tail index pointing at a slot whose
        // new contents have not landed yet, a live data race on slots_[].
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer-only. Returns false if the ring is empty. Never blocks,
    // never spins internally.
    bool try_pop(T& out) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        // Acquire: pairs with the producer's release store to tail_ above.
        // Seeing the updated tail_ here also makes the producer's write to
        // slots_[head] visible before this function reads it.
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;  // empty
        }
        out = slots_[head];
        // Release: publishes "this slot is free again" to the producer's
        // next acquire-load of head_, the same pairing in the other
        // direction.
        head_.store(advance(head), std::memory_order_release);
        return true;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

private:
    [[nodiscard]] static constexpr std::size_t advance(std::size_t index) noexcept {
        return (index + 1) & mask;
    }

    static constexpr std::size_t mask = Capacity - 1;

    alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> head_{0};
    alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> tail_{0};
    std::array<T, Capacity> slots_{};
};

}  // namespace ch05
