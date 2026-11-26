#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

#include "ch07/cache_result.hpp"
#include "ch07/route_resolver.hpp"
#include "ch07/ttl_lru_cache.hpp"

namespace ch07 {

// Same shape as every prior chapter's raw per-access duration (see ch03's
// Sample): not yet fed into a measure::LatencyRecorder.
using Sample = std::chrono::duration<double>;

[[nodiscard]] inline double hit_ratio(std::size_t hits, std::size_t misses) {
    const std::size_t total = hits + misses;
    return total == 0 ? 0.0 : static_cast<double>(hits) / static_cast<double>(total);
}

// One lane's outcome: the per-access latency distribution plus the
// bookkeeping every lane's correctness check needs. `mismatch_count` counts
// accesses where the served value disagreed with RouteResolver::ground_truth
// at the moment of that access; for every eviction-policy lane this should
// read 0 (the backing table never changes mid-trial for those lanes, so a
// cached value can never go stale), which is the lane's own behavioral
// check, not just a hit-ratio report.
struct CacheTrialResult {
    std::vector<Sample> samples;
    std::size_t hit_count = 0;
    std::size_t miss_count = 0;
    std::size_t mismatch_count = 0;
};

// The uncached baseline: every access in `trace` pays RouteResolver::resolve
// directly, with nothing in front of it. This is what every cached lane is
// measured against.
[[nodiscard]] CacheTrialResult run_uncached_trial(const RouteResolver& resolver, const std::vector<int>& trace);

// Drives `trace` through any of this chapter's eviction-policy caches
// (LruCache, LfuCache, FifoCache, SieveCache all satisfy this shape), timing
// every access and loading a miss from `resolver`.
template <typename Cache>
[[nodiscard]] CacheTrialResult run_cache_trial(Cache& cache, const RouteResolver& resolver,
                                                const std::vector<int>& trace) {
    CacheTrialResult result;
    result.samples.reserve(trace.size());
    for (const int key : trace) {
        const auto started_at = std::chrono::steady_clock::now();
        const CacheResult outcome = cache.get_or_insert(key, [&resolver](int k) { return resolver.resolve(k); });
        const auto finished_at = std::chrono::steady_clock::now();
        result.samples.emplace_back(finished_at - started_at);
        outcome.hit ? ++result.hit_count : ++result.miss_count;
        if (outcome.value != resolver.ground_truth(key)) {
            ++result.mismatch_count;
        }
    }
    return result;
}

// A TTL lane's outcome adds one thing a plain eviction-policy lane cannot
// produce: `stale_serve_count`, the number of post-reroute accesses that
// were served from cache with a value that no longer matched
// RouteResolver::ground_truth at the moment they were served. A hit before
// `reroute_at_index` can never be stale (nothing has changed yet); only a
// hit after it can be, and only if the TTL window let the cache go that
// long without re-checking the source.
struct StalenessTrialResult {
    std::vector<Sample> samples;
    std::size_t hit_count = 0;
    std::size_t miss_count = 0;
    std::size_t stale_serve_count = 0;
};

// Runs `trace` through `cache`, rerouting `reroute_keys` in `resolver` right
// after access index `reroute_at_index`. Uses a synthetic clock (`epoch +
// access_index * tick`) rather than the real one, so a TTL window means the
// identical number of accesses on every host instead of however fast that
// host happens to run this loop.
[[nodiscard]] StalenessTrialResult run_ttl_trial(TtlLruCache& cache, RouteResolver& resolver,
                                                  const std::vector<int>& trace,
                                                  const std::vector<int>& reroute_keys,
                                                  std::size_t reroute_at_index,
                                                  std::chrono::steady_clock::duration tick);

}  // namespace ch07
