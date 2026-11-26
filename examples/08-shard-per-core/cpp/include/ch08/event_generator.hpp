#pragma once

#include <cstdint>
#include <vector>

#include "ch08/tracked_state.hpp"

namespace ch08 {

// A uniform-random trace over [1, key_space]: every lane sees the same
// shape of load, no key favored over another. This is the trace the main
// shared-lock-versus-sharded comparison runs, because that comparison asks
// only how the two trackers handle contention, not how either handles
// skew.
[[nodiscard]] std::vector<Barcode> generate_uniform_events(std::uint64_t count, std::uint64_t key_space,
                                                            std::uint64_t seed);

// A skewed trace: hot_fraction of `count` events draw uniformly from
// `hot_keys` (a handful of barcodes, typically found by
// find_colliding_hot_keys so they land on the same shard under direct hash
// routing); the remaining events draw uniformly from a disjoint cold key
// space of `cold_key_space` keys that never collides with a hot key's
// value. This is the shape a handful of fast-moving SKUs produce against a
// catalog of thousands (see "Hot-partition skew").
[[nodiscard]] std::vector<Barcode> generate_skewed_events(std::uint64_t count,
                                                           const std::vector<Barcode>& hot_keys,
                                                           double hot_fraction, std::uint64_t cold_key_space,
                                                           std::uint64_t seed);

}  // namespace ch08
