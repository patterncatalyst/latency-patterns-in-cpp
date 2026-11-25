#pragma once

#include <cstddef>
#include <unordered_map>
#include <utility>

namespace ch07 {

// Memoizes a pure function: a cache that never evicts and never expires,
// because the one thing it needs to be sound, that f(key) never changes for
// a given key, is true by construction for a pure function. Contrast with
// every eviction-managed cache elsewhere in this chapter: those exist
// because a bounded cache in front of a source that CAN change needs a
// policy for what to drop; a memoized pure function needs no policy at all,
// only somewhere to remember an answer it will never have to revise.
template <typename Key, typename Value, typename Func>
class Memoizer {
public:
    explicit Memoizer(Func func) : func_(std::move(func)) {}

    const Value& operator()(const Key& key) {
        if (auto found = cache_.find(key); found != cache_.end()) {
            ++hit_count_;
            return found->second;
        }
        ++miss_count_;
        auto [inserted, _] = cache_.emplace(key, func_(key));
        return inserted->second;
    }

    [[nodiscard]] std::size_t hit_count() const noexcept { return hit_count_; }
    [[nodiscard]] std::size_t miss_count() const noexcept { return miss_count_; }
    [[nodiscard]] std::size_t size() const noexcept { return cache_.size(); }

private:
    Func func_;
    std::unordered_map<Key, Value> cache_;
    std::size_t hit_count_ = 0;
    std::size_t miss_count_ = 0;
};

}  // namespace ch07
