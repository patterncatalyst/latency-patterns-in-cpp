#include "ch05/mutex_handoff_queue.hpp"

namespace ch05 {

void MutexHandoffQueue::push(Decision decision) {
    {
        const std::scoped_lock lock(mutex_);
        queue_.push(decision);
    }
    // Notified outside the lock: waking a waiter while still holding the
    // mutex only means it immediately blocks again on the lock it needs to
    // re-check its predicate, one extra futex round trip for nothing.
    not_empty_.notify_one();
}

bool MutexHandoffQueue::pop(Decision& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) {
        return false;  // closed_ with nothing left
    }
    out = queue_.front();
    queue_.pop();
    return true;
}

void MutexHandoffQueue::close() {
    {
        const std::scoped_lock lock(mutex_);
        closed_ = true;
    }
    not_empty_.notify_all();
}

}  // namespace ch05
