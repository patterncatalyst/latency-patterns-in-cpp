#pragma once

#include <cstddef>
#include <list>
#include <unordered_map>

#include "ch07/cache_result.hpp"

namespace ch07 {

// Least-frequently-used: evict whichever cached key has been touched the
// fewest times, breaking ties by least-recently-touched within that
// frequency. Constant-time per operation via frequency buckets: `nodes_`
// holds each key's value and current access count, `buckets_[f]` holds
// every key currently at frequency f (most-recently-touched-within-f at the
// front), and `min_freq_` tracks the lowest non-empty bucket so eviction
// never has to scan for the minimum.
class LfuCache {
public:
    explicit LfuCache(std::size_t capacity) : capacity_(capacity) {}

    template <typename Loader>
    CacheResult get_or_insert(int key, Loader loader) {
        if (auto found = nodes_.find(key); found != nodes_.end()) {
            touch(key);
            return CacheResult{.value = found->second.value, .hit = true};
        }

        const int value = loader(key);
        if (nodes_.size() >= capacity_) {
            std::list<int>& victims = buckets_[min_freq_];
            const int victim = victims.back();
            victims.pop_back();
            positions_.erase(victim);
            nodes_.erase(victim);
        }
        nodes_.emplace(key, Node{.value = value, .freq = 1});
        buckets_[1].push_front(key);
        positions_[key] = buckets_[1].begin();
        min_freq_ = 1;
        return CacheResult{.value = value, .hit = false};
    }

    [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }

private:
    struct Node {
        int value;
        int freq;
    };

    void touch(int key) {
        Node& node = nodes_.at(key);
        const int old_freq = node.freq;
        buckets_[old_freq].erase(positions_.at(key));
        if (buckets_[old_freq].empty() && min_freq_ == old_freq) {
            ++min_freq_;
        }
        ++node.freq;
        buckets_[node.freq].push_front(key);
        positions_[key] = buckets_[node.freq].begin();
    }

    std::size_t capacity_;
    int min_freq_ = 0;
    std::unordered_map<int, Node> nodes_;
    std::unordered_map<int, std::list<int>> buckets_;
    std::unordered_map<int, std::list<int>::iterator> positions_;
};

}  // namespace ch07
