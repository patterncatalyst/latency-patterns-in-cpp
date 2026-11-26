#include "ch07/cache_trial.hpp"

#include <chrono>

namespace ch07 {

CacheTrialResult run_uncached_trial(const RouteResolver& resolver, const std::vector<int>& trace) {
    CacheTrialResult result;
    result.samples.reserve(trace.size());
    for (const int key : trace) {
        const auto started_at = std::chrono::steady_clock::now();
        const int value = resolver.resolve(key);
        const auto finished_at = std::chrono::steady_clock::now();
        result.samples.emplace_back(finished_at - started_at);
        ++result.miss_count;  // there is no cache here, so every access is a miss by definition
        if (value != resolver.ground_truth(key)) {
            ++result.mismatch_count;
        }
    }
    return result;
}

StalenessTrialResult run_ttl_trial(TtlLruCache& cache, RouteResolver& resolver, const std::vector<int>& trace,
                                    const std::vector<int>& reroute_keys, std::size_t reroute_at_index,
                                    std::chrono::steady_clock::duration tick) {
    StalenessTrialResult result;
    result.samples.reserve(trace.size());

    // A synthetic clock: access i is deemed to happen at epoch + i * tick,
    // not at whatever the real clock reads when this loop gets there. That
    // makes "how many accesses after reroute before this entry expires" a
    // host-independent count instead of a function of how fast this run
    // happened to go.
    const auto epoch = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < trace.size(); ++i) {
        if (i == reroute_at_index) {
            resolver.reroute(reroute_keys);
        }
        const int key = trace[i];
        const auto synthetic_now = epoch + (static_cast<std::int64_t>(i) * tick);

        const auto started_at = std::chrono::steady_clock::now();
        const CacheResult outcome =
            cache.get_or_insert(key, [&resolver](int k) { return resolver.resolve(k); }, synthetic_now);
        const auto finished_at = std::chrono::steady_clock::now();
        result.samples.emplace_back(finished_at - started_at);

        outcome.hit ? ++result.hit_count : ++result.miss_count;
        if (i >= reroute_at_index && outcome.hit && outcome.value != resolver.ground_truth(key)) {
            ++result.stale_serve_count;
        }
    }
    return result;
}

}  // namespace ch07
