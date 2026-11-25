#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

#include "ch05/decision.hpp"

namespace ch05 {

// The "before" handoff: one mutex, one condition_variable, and an unbounded
// std::queue. Idiomatic, textbook producer/consumer, and the thing this
// chapter's wait-free ring replaces: push() and pop() both take the same
// lock on every single item, and a divert thread with nothing to do sleeps
// in the condition_variable rather than spinning, which means waking it
// costs a futex round trip through the kernel scheduler (see the chapter
// prose, "The mutex handoff and where it blocks").
//
// Single-producer/single-consumer in this chapter's usage, but nothing here
// assumes that: the mutex makes any number of producers and consumers safe,
// which is exactly the generality the wait-free ring gives up to win.
class MutexHandoffQueue {
public:
    void push(Decision decision);

    // Blocks until an item is available or the queue has been closed with
    // nothing left to deliver. Returns false only in that second case, so a
    // consumer can loop `while (queue.pop(out))` to drain to completion.
    bool pop(Decision& out);

    // Called once by the producer after its last push(). Wakes a consumer
    // blocked in pop() so it observes "empty and closed" and returns false
    // instead of waiting forever.
    void close();

private:
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::queue<Decision> queue_;
    bool closed_ = false;
};

}  // namespace ch05
