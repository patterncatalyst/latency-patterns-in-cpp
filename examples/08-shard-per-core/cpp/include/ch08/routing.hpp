#pragma once

#include <cstddef>
#include <vector>

#include "ch08/tracked_state.hpp"

namespace ch08 {

// Direct hash routing: a barcode maps to exactly one shard, computed fresh
// every call from the key alone. This is the whole mechanism; there is
// nothing to look up and nothing to rebalance. mix64 (splitmix64's
// finalizer) is used instead of std::hash<Barcode> because this book's
// barcodes are small sequential integers in the trials below, and
// std::hash<std::uint64_t> is the identity function on every mainstream
// standard library, which would route sequential keys round-robin rather
// than uniformly -- fine for throughput, useless for the skew lane, which
// needs an ordinary-looking key to land wherever its bits say it lands.
[[nodiscard]] std::uint64_t mix64(std::uint64_t x) noexcept;

[[nodiscard]] std::size_t hash_route(Barcode key, std::size_t shard_count) noexcept;

// The routing indirection: a key maps to one of bucket_count buckets by
// mix64, and a small table maps each bucket to a shard. With
// bucket_count == shard_count and every bucket assigned to its own index,
// this is indistinguishable from hash_route(). The indirection earns its
// keep the moment a bucket gets reassigned: moving one bucket's traffic to
// a different shard (assign()) needs no rehash and touches no other
// bucket's assignment (see "Request routing" and the skew mitigation it
// drives).
class RoutingTable {
public:
    RoutingTable(std::size_t bucket_count, std::size_t shard_count);

    [[nodiscard]] std::size_t bucket_count() const noexcept { return table_.size(); }
    [[nodiscard]] std::size_t shard_count() const noexcept { return shard_count_; }

    [[nodiscard]] std::size_t bucket_for_key(Barcode key) const noexcept;
    [[nodiscard]] std::size_t shard_for_bucket(std::size_t bucket) const;
    [[nodiscard]] std::size_t route(Barcode key) const;

    // Reassigns one bucket to a different shard. O(1); nothing else in the
    // table changes, and every key outside this bucket keeps its existing
    // shard.
    void assign(std::size_t bucket, std::size_t shard);

private:
    std::size_t shard_count_;
    std::vector<std::size_t> table_;  // table_[bucket] = shard
};

// Scans barcodes 1, 2, 3, ... under hash_route(*, shard_count) and returns
// the first `needed` keys that land on the identical shard. shard_count
// varies by host (it is set from std::thread::hardware_concurrency(), see
// main.cpp), so this chapter cannot hardcode which barcodes collide; it
// searches for a real collision under whatever hash_route actually does on
// this run, which the pigeonhole principle guarantees exists within
// shard_count + 1 distinct keys for a 2-key collision. Returns an empty
// vector only if `needed` collide nowhere in the scanned range, which
// should not happen for any shard_count this book's hosts report.
[[nodiscard]] std::vector<Barcode> find_colliding_hot_keys(std::size_t shard_count, std::size_t needed,
                                                            std::uint64_t scan_limit = 1'000'000);

}  // namespace ch08
