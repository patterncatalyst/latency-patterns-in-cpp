#pragma once

#include <cstddef>
#include <list>
#include <unordered_map>
#include <utility>

#include "ch07/cache_result.hpp"

namespace ch07 {

// Least-recently-used: evict whichever cached key has gone the longest
// without being touched. `order_` holds keys from most-recently-used
// (front) to least-recently-used (back); `map_` holds each key's value plus
// an iterator into `order_` so both a hit's move-to-front and an eviction's
// pop-from-back are O(1), the textbook std::list + std::unordered_map
// shape for an LRU.
class LruCache {
public:
    explicit LruCache(std::size_t capacity) : capacity_(capacity) {}

    template <typename Loader>
    CacheResult get_or_insert(int key, Loader loader) {
        if (auto found = map_.find(key); found != map_.end()) {
            order_.splice(order_.begin(), order_, found->second.position);
            return CacheResult{.value = found->second.value, .hit = true};
        }

        const int value = loader(key);
        if (map_.size() >= capacity_) {
            const int victim = order_.back();
            order_.pop_back();
            map_.erase(victim);
        }
        order_.push_front(key);
        map_.emplace(key, Entry{.value = value, .position = order_.begin()});
        return CacheResult{.value = value, .hit = false};
    }

    [[nodiscard]] std::size_t size() const noexcept { return map_.size(); }

private:
    struct Entry {
        int value;
        std::list<int>::iterator position;
    };

    std::size_t capacity_;
    std::list<int> order_;
    std::unordered_map<int, Entry> map_;
};

}  // namespace ch07
