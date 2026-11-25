#pragma once

#include <cstddef>
#include <list>
#include <unordered_map>

#include "ch07/cache_result.hpp"

namespace ch07 {

// SIEVE: a FIFO insertion queue plus one bit per entry ("has this been
// touched since it was last examined for eviction?") and one moving "hand"
// that remembers where the last eviction scan left off.
//
// `entries_` holds every cached (key, value, visited) triple with the most
// recently INSERTED entry at the front and the oldest at the back; a hit
// never moves an entry, it only sets that entry's `visited` bit, which is
// the entire cost SIEVE pays on the fast path. Eviction walks from the hand
// (or the back, the first time) towards the front: an entry whose bit is
// set gets a second chance (the bit clears, the scan moves on); the first
// entry found with the bit still clear is the victim, and the hand is left
// at the entry before it so the next eviction resumes there instead of
// rescanning what it already cleared.
class SieveCache {
public:
    explicit SieveCache(std::size_t capacity) : capacity_(capacity) {}

    template <typename Loader>
    CacheResult get_or_insert(int key, Loader loader) {
        if (auto found = index_.find(key); found != index_.end()) {
            found->second->visited = true;  // SIEVE's only reaction to a hit
            return CacheResult{.value = found->second->value, .hit = true};
        }

        const int value = loader(key);
        if (index_.size() >= capacity_) {
            evict();
        }
        entries_.push_front(Entry{.key = key, .value = value, .visited = false});
        index_[key] = entries_.begin();
        return CacheResult{.value = value, .hit = false};
    }

    [[nodiscard]] std::size_t size() const noexcept { return index_.size(); }

private:
    struct Entry {
        int key;
        int value;
        bool visited;
    };
    using Iter = std::list<Entry>::iterator;

    // Moves `it` one step towards the front, wrapping to the back once it
    // reaches the front: the circular scan direction the SIEVE hand walks.
    Iter step_toward_front(Iter it) {
        return (it == entries_.begin()) ? std::prev(entries_.end()) : std::prev(it);
    }

    void evict() {
        auto candidate = (hand_ == entries_.end()) ? std::prev(entries_.end()) : hand_;
        while (candidate->visited) {
            candidate->visited = false;
            candidate = step_toward_front(candidate);
        }
        hand_ = (candidate == entries_.begin()) ? entries_.end() : std::prev(candidate);
        index_.erase(candidate->key);
        entries_.erase(candidate);
    }

    std::size_t capacity_;
    std::list<Entry> entries_;
    std::unordered_map<int, Iter> index_;
    Iter hand_ = entries_.end();
};

}  // namespace ch07
