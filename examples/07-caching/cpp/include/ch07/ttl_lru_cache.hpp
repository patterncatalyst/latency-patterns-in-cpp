#pragma once

#include <chrono>
#include <cstddef>
#include <list>
#include <unordered_map>

#include "ch07/cache_result.hpp"

namespace ch07 {

// An LRU cache with one more rule: an entry older than `ttl` is treated as
// absent, even though it is still physically sitting in `map_`. This is how
// a cache in front of a source that can change underneath it (a re-route)
// bounds how long it can keep serving an answer that is no longer true,
// without ever being told the source changed.
//
// `now` is passed in rather than read from the clock internally so a test
// (and this chapter's trial) can drive expiry deterministically instead of
// racing a real clock; see cache_trial.hpp for how the trial turns "access
// number" into a synthetic `now` so a TTL window means the same number of
// accesses on every host.
class TtlLruCache {
public:
    TtlLruCache(std::size_t capacity, std::chrono::steady_clock::duration ttl)
        : capacity_(capacity), ttl_(ttl) {}

    template <typename Loader>
    CacheResult get_or_insert(int key, Loader loader, std::chrono::steady_clock::time_point now) {
        if (auto found = map_.find(key); found != map_.end()) {
            if (now - found->second.inserted_at < ttl_) {
                order_.splice(order_.begin(), order_, found->second.position);
                return CacheResult{.value = found->second.value, .hit = true};
            }
            // Expired: this slot is stale, not useful. Drop it and fall
            // through to the miss path exactly as if it had never been
            // cached, including paying for a fresh insert's eviction check.
            order_.erase(found->second.position);
            map_.erase(found);
        }

        const int value = loader(key);
        if (map_.size() >= capacity_) {
            const int victim = order_.back();
            order_.pop_back();
            map_.erase(victim);
        }
        order_.push_front(key);
        map_.emplace(key, Entry{.value = value, .inserted_at = now, .position = order_.begin()});
        return CacheResult{.value = value, .hit = false};
    }

    [[nodiscard]] std::size_t size() const noexcept { return map_.size(); }

private:
    struct Entry {
        int value;
        std::chrono::steady_clock::time_point inserted_at;
        std::list<int>::iterator position;
    };

    std::size_t capacity_;
    std::chrono::steady_clock::duration ttl_;
    std::list<int> order_;
    std::unordered_map<int, Entry> map_;
};

}  // namespace ch07
