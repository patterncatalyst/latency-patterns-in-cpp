#pragma once

#include <cstddef>
#include <deque>
#include <unordered_map>

#include "ch07/cache_result.hpp"

namespace ch07 {

// First-in-first-out: evict whichever cached key was inserted longest ago,
// regardless of how recently or how often it has been accessed since. The
// simplest of this chapter's four policies: a plain insertion-order queue
// plus a map, with no bookkeeping updated on a hit at all.
class FifoCache {
public:
    explicit FifoCache(std::size_t capacity) : capacity_(capacity) {}

    template <typename Loader>
    CacheResult get_or_insert(int key, Loader loader) {
        if (auto found = map_.find(key); found != map_.end()) {
            return CacheResult{.value = found->second, .hit = true};  // a hit never reorders the queue
        }

        const int value = loader(key);
        if (map_.size() >= capacity_) {
            const int victim = insertion_order_.front();
            insertion_order_.pop_front();
            map_.erase(victim);
        }
        insertion_order_.push_back(key);
        map_.emplace(key, value);
        return CacheResult{.value = value, .hit = false};
    }

    [[nodiscard]] std::size_t size() const noexcept { return map_.size(); }

private:
    std::size_t capacity_;
    std::deque<int> insertion_order_;
    std::unordered_map<int, int> map_;
};

}  // namespace ch07
