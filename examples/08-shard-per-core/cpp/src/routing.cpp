#include "ch08/routing.hpp"

#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace ch08 {

std::uint64_t mix64(std::uint64_t x) noexcept {
    // splitmix64's finalizer (Vigna, public domain): three xorshift/multiply
    // rounds, good avalanche for an integer key this small and this
    // sequential. Not a general-purpose hash function; it exists here only
    // to spread Barcode values across shard indices.
    x ^= x >> 30U;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27U;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31U;
    return x;
}

std::size_t hash_route(Barcode key, std::size_t shard_count) noexcept {
    return shard_count == 0 ? 0 : static_cast<std::size_t>(mix64(key) % shard_count);
}

RoutingTable::RoutingTable(std::size_t bucket_count, std::size_t shard_count) : shard_count_(shard_count) {
    if (bucket_count == 0 || shard_count == 0) {
        throw std::invalid_argument("RoutingTable: bucket_count and shard_count must both be positive");
    }
    table_.resize(bucket_count);
    // Default assignment: bucket i starts on shard i % shard_count, the
    // identical split hash_route() would produce for a bucket_count that
    // divides evenly. Rebalancing moves individual buckets off this
    // default; it never changes the default itself.
    for (std::size_t bucket = 0; bucket < bucket_count; ++bucket) {
        table_[bucket] = bucket % shard_count;
    }
}

std::size_t RoutingTable::bucket_for_key(Barcode key) const noexcept {
    return static_cast<std::size_t>(mix64(key) % table_.size());
}

std::size_t RoutingTable::shard_for_bucket(std::size_t bucket) const {
    return table_.at(bucket);
}

std::size_t RoutingTable::route(Barcode key) const {
    return table_[bucket_for_key(key)];
}

void RoutingTable::assign(std::size_t bucket, std::size_t shard) {
    if (shard >= shard_count_) {
        throw std::out_of_range("RoutingTable::assign: shard index out of range");
    }
    table_.at(bucket) = shard;
}

std::vector<Barcode> find_colliding_hot_keys(std::size_t shard_count, std::size_t needed,
                                              std::uint64_t scan_limit) {
    std::unordered_map<std::size_t, std::vector<Barcode>> by_shard;
    for (Barcode candidate = 1; candidate <= scan_limit; ++candidate) {
        std::vector<Barcode>& bucket = by_shard[hash_route(candidate, shard_count)];
        bucket.push_back(candidate);
        if (bucket.size() == needed) {
            return bucket;
        }
    }
    return {};
}

}  // namespace ch08
