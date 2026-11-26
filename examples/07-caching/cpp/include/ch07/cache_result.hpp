#pragma once

namespace ch07 {

// The common answer shape every cache below hands back from get_or_insert:
// the resolved value, plus whether it was already sitting in the cache
// (a hit) or had to come from the loader (a miss). Every replacement
// policy in this chapter (LruCache, LfuCache, FifoCache, SieveCache,
// TtlLruCache) returns this same shape so the trial runner in
// cache_trial.hpp can drive all of them through one loop.
struct CacheResult {
    int value;
    bool hit;
};

}  // namespace ch07
