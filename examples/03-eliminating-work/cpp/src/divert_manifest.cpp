#include "ch03/divert_manifest.hpp"

#include <cstddef>

namespace ch03 {

namespace {
constexpr std::uint64_t decode_hash_multiplier = 2654435761ULL;  // Knuth's multiplicative hash
constexpr unsigned int decode_hash_shift = 13;
// Modest on purpose: this chapter's point is the per-parcel waste AROUND the
// decode, not the decode's own cost (see decode_key_for_barcode). A larger
// value here still leaves the decode cost identical between the two lanes;
// it would only shrink the relative size of the effect this chapter
// measures, never which lane wins.
constexpr int decode_iterations = 10;

constexpr std::uint64_t manifest_hash_multiplier = 2246822519ULL;  // a second, distinct odd constant
constexpr unsigned int manifest_hash_shift = 15;

constexpr std::uint64_t zone_hash_multiplier = 3266489917ULL;
constexpr unsigned int zone_hash_shift = 17;
constexpr double zone_multiplier_floor = 0.80;
constexpr std::uint64_t zone_multiplier_band = 4001;  // maps into [0.80, 1.20)
constexpr double zone_multiplier_scale = 10000.0;
}  // namespace

int decode_key_for_barcode(std::uint64_t barcode) noexcept {
    std::uint64_t checksum = barcode;
    for (int i = 0; i < decode_iterations; ++i) {
        checksum = (checksum * decode_hash_multiplier) ^ (checksum >> decode_hash_shift);
    }
    return static_cast<int>(checksum % static_cast<std::uint64_t>(key_space));
}

int manifest_chute_for_key(int dense_key) noexcept {
    std::uint64_t mixed = static_cast<std::uint64_t>(dense_key) * manifest_hash_multiplier;
    mixed ^= mixed >> manifest_hash_shift;
    return static_cast<int>(mixed % static_cast<std::uint64_t>(chute_count));
}

double manifest_zone_multiplier_for_chute(int chute) noexcept {
    std::uint64_t mixed = (static_cast<std::uint64_t>(chute) + 1) * zone_hash_multiplier;
    mixed ^= mixed >> zone_hash_shift;
    return zone_multiplier_floor +
           (static_cast<double>(mixed % zone_multiplier_band) / zone_multiplier_scale);
}

std::unordered_map<int, int> build_manifest_map() {
    std::unordered_map<int, int> map;
    map.reserve(static_cast<std::size_t>(key_space));
    for (int key = 0; key < key_space; ++key) {
        map.emplace(key, manifest_chute_for_key(key));
    }
    return map;
}

std::array<int, key_space> build_manifest_table() {
    std::array<int, key_space> table{};
    for (int key = 0; key < key_space; ++key) {
        table[static_cast<std::size_t>(key)] = manifest_chute_for_key(key);
    }
    return table;
}

}  // namespace ch03
