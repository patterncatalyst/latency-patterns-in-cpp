#pragma once

#include <sys/eventfd.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <stdexcept>
#include <vector>

namespace ch09 {

// The hot-path-to-consumer handoff. Single-producer (the hot path thread),
// single-consumer (the background event-loop thread), built the same
// wait-free way as "Wait-free synchronization"'s SpscRingQueue: head_ and
// tail_ are each the only thing their cache line holds, and the fast path
// (room available) touches nothing but those two atomics. What that
// chapter's ring deliberately left out, "the caller decides whether to
// retry or drop," is exactly what a hot path cannot do: looping on
// try_push is the same synchronous stall this chapter exists to remove.
// push_with_backpressure() instead waits, bounded by max_wait, for the
// consumer to make room, and sheds (with accounting, never silently) only
// once that bound is spent. The bound, not an unbounded retry loop, is the
// backpressure contract.
// clang-analyzer's padding check would reorder head_ and tail_ next to each
// other to shrink this class; that would undo the alignas placement below,
// which exists precisely so the producer writing tail_ and the consumer
// writing head_ never share a cache line (see "Zero-copy data layout" and
// "Wait-free synchronization," this ring's ancestor, which makes the same
// trade). The padding here is deliberate, not accidental.
template <typename T, std::size_t Capacity>
class BoundedHandoffQueue {  // NOLINT(clang-analyzer-optin.performance.Padding)
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two, at least 2");

public:
    BoundedHandoffQueue() : event_fd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {
        if (event_fd_ < 0) {
            throw std::runtime_error("BoundedHandoffQueue: eventfd() failed");
        }
    }

    ~BoundedHandoffQueue() {
        if (event_fd_ >= 0) {
            ::close(event_fd_);
        }
    }

    BoundedHandoffQueue(const BoundedHandoffQueue&) = delete;
    BoundedHandoffQueue& operator=(const BoundedHandoffQueue&) = delete;
    BoundedHandoffQueue(BoundedHandoffQueue&&) = delete;
    BoundedHandoffQueue& operator=(BoundedHandoffQueue&&) = delete;

    // Producer-only (the hot path). Never blocks longer than max_wait.
    // Returns true once the record is in the ring and the consumer has been
    // signaled; returns false if the ring was still full once max_wait ran
    // out, in which case the caller's record was shed, not queued.
    bool push_with_backpressure(T value, std::chrono::microseconds max_wait) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = advance(tail);
        if (next == head_.load(std::memory_order_acquire)) {
            std::unique_lock lock(backpressure_mutex_);
            const bool made_room = backpressure_cv_.wait_for(
                lock, max_wait, [&] { return next != head_.load(std::memory_order_acquire); });
            if (!made_room) {
                shed_count_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        }
        slots_[tail] = std::move(value);
        // Release: publishes the slot write above; the consumer's acquire
        // load of tail_ (see drain()) is guaranteed to see it, the same
        // pairing "Wait-free synchronization" used for this ring's ancestor.
        tail_.store(next, std::memory_order_release);
        signal_consumer();
        update_high_water_mark(depth_after_push(next));
        return true;
    }

    // Consumer-only. Pops up to max_items into out (append; out is not
    // cleared here, so the caller controls when a batch buffer resets).
    // Returns the number popped. Never blocks, never allocates: out must
    // already have the room it needs (see BufferPool).
    std::size_t drain(std::vector<T>& out, std::size_t max_items) {
        std::size_t popped = 0;
        std::size_t head = head_.load(std::memory_order_relaxed);
        while (popped < max_items) {
            if (head == tail_.load(std::memory_order_acquire)) {
                break;
            }
            out.push_back(slots_[head]);
            head = advance(head);
            ++popped;
        }
        if (popped > 0) {
            head_.store(head, std::memory_order_release);
            backpressure_cv_.notify_all();
        }
        return popped;
    }

    [[nodiscard]] int event_fd() const noexcept { return event_fd_; }
    [[nodiscard]] std::uint64_t shed_count() const noexcept { return shed_count_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t high_water_mark() const noexcept {
        return high_water_mark_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

    // Clears the eventfd's counter. Call after epoll reports it readable;
    // eventfd is level-triggered on its counter value, not edge-triggered
    // on each write(), so the counter must be drained or epoll reports it
    // ready again immediately.
    void clear_event() const noexcept {
        std::uint64_t value = 0;
        [[maybe_unused]] const ssize_t ignored = ::read(event_fd_, &value, sizeof(value));
    }

private:
    static constexpr std::size_t mask = Capacity - 1;

    [[nodiscard]] static constexpr std::size_t advance(std::size_t index) noexcept { return (index + 1) & mask; }

    [[nodiscard]] std::size_t depth_after_push(std::size_t tail_after) const noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        return (tail_after - head) & mask;
    }

    void update_high_water_mark(std::size_t depth) noexcept {
        std::uint64_t current = high_water_mark_.load(std::memory_order_relaxed);
        const auto candidate = static_cast<std::uint64_t>(depth);
        while (candidate > current &&
               !high_water_mark_.compare_exchange_weak(current, candidate, std::memory_order_relaxed)) {
        }
    }

    void signal_consumer() const noexcept {
        constexpr std::uint64_t one = 1;
        [[maybe_unused]] const ssize_t ignored = ::write(event_fd_, &one, sizeof(one));
    }

    alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> head_{0};
    alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> tail_{0};
    std::array<T, Capacity> slots_{};
    std::mutex backpressure_mutex_;
    std::condition_variable backpressure_cv_;
    std::atomic<std::uint64_t> shed_count_{0};
    std::atomic<std::uint64_t> high_water_mark_{0};
    int event_fd_;
};

}  // namespace ch09
