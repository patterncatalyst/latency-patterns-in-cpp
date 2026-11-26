#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ch07 {

// Generates a Zipf-skewed access trace over [0, key_space): a small hot set
// takes most of the traffic, with a long tail of rarely-seen keys. This is
// the access shape that makes a replacement policy's choice matter; on a
// uniform-random trace every policy sees the same mostly-cold working set
// and none of them beats the others (see the chapter prose, "Why the
// policies need a skewed trace").
//
// The mapping from "popularity rank" to "which key is that popular" is
// shuffled with its own seed, independent of the rank order itself, so a
// hot key is not systematically the one RouteResolver can resolve fastest
// (see RouteResolver's comment on why entries_ is ordered by key value, not
// by popularity).
class ZipfTrace {
public:
    ZipfTrace(int key_space, double skew, std::uint64_t seed);

    // Produces `length` accesses, each an independent draw from the
    // Zipf distribution over the shuffled rank-to-key mapping.
    [[nodiscard]] std::vector<int> generate(std::size_t length) const;

    // The `count` most popular keys (rank 0..count-1 after the shuffle),
    // used by the chapter's TTL trial to reroute exactly the keys a cache is
    // most likely to still be holding when the reroute happens.
    [[nodiscard]] std::vector<int> hottest_keys(int count) const;

private:
    int key_space_;
    std::uint64_t seed_;
    std::vector<double> cumulative_;  // cumulative_[r] = P(rank <= r)
    std::vector<int> rank_to_key_;    // shuffled: rank r resolves to this key
};

}  // namespace ch07
