#pragma once

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace ch09 {

// A pool of reusable batch buffers. The background consumer acquires one
// per flush cycle, fills it with up to buffer_capacity records, hands it to
// the writer, then releases it back to the pool instead of letting a
// std::vector<T> get allocated and freed every single batch. Only the
// consumer thread touches this (it is off the hot path by construction, see
// EventLoopConsumer), so one mutex around the free list is the right amount
// of synchronization for it, not none: this is not a hot-path data
// structure, and pretending otherwise would be the wrong kind of clever.
template <typename T>
class BufferPool {
public:
    BufferPool(std::size_t buffer_count, std::size_t buffer_capacity) : buffer_capacity_(buffer_capacity) {
        free_list_.reserve(buffer_count);
        for (std::size_t i = 0; i < buffer_count; ++i) {
            free_list_.push_back(make_buffer());
        }
    }

    std::unique_ptr<std::vector<T>> acquire() {
        const std::scoped_lock lock(mutex_);
        if (free_list_.empty()) {
            // The pool ran dry (more batches in flight than buffers
            // provisioned); fall back to allocating one rather than
            // blocking the consumer, and count it so a chapter run can
            // confirm the steady-state pool never needed to.
            ++allocations_beyond_pool_;
            return make_buffer();
        }
        auto buf = std::move(free_list_.back());
        free_list_.pop_back();
        return buf;
    }

    void release(std::unique_ptr<std::vector<T>> buf) {
        buf->clear();
        const std::scoped_lock lock(mutex_);
        free_list_.push_back(std::move(buf));
    }

    [[nodiscard]] std::size_t allocations_beyond_pool() const noexcept {
        const std::scoped_lock lock(mutex_);
        return allocations_beyond_pool_;
    }

private:
    std::unique_ptr<std::vector<T>> make_buffer() const {
        auto buf = std::make_unique<std::vector<T>>();
        buf->reserve(buffer_capacity_);
        return buf;
    }

    std::size_t buffer_capacity_;
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<std::vector<T>>> free_list_;
    std::size_t allocations_beyond_pool_ = 0;
};

}  // namespace ch09
