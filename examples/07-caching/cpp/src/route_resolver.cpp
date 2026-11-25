#include "ch07/route_resolver.hpp"

#include <algorithm>

namespace ch07 {

int manifest_chute_for_key(int dense_key) noexcept {
    // Same shape as ch03::manifest_chute_for_key: a multiplicative hash
    // folded down into a small chute range, fixed and deterministic for a
    // given key, arbitrary otherwise.
    constexpr int chute_count = 16;
    const std::uint32_t mixed = static_cast<std::uint32_t>(dense_key) * 2654435761U;
    return static_cast<int>(mixed >> 24U) % chute_count;
}

RouteResolver::RouteResolver(int key_space) {
    entries_.reserve(static_cast<std::size_t>(key_space));
    for (int key = 0; key < key_space; ++key) {
        entries_.push_back(RouteEntry{.key = key, .chute = manifest_chute_for_key(key)});
    }
}

int RouteResolver::resolve(int barcode_key) const {
    std::uint64_t checksum = 0xA24BAED4CB1A1B6FULL;
    for (const RouteEntry& entry : entries_) {
        checksum = (checksum * 1099511628211ULL) ^ static_cast<std::uint64_t>(entry.key);
        if (entry.key == barcode_key) {
            scan_checksum_ = checksum;
            return entry.chute;
        }
    }
    scan_checksum_ = checksum;
    return -1;  // barcode_key outside [0, key_space); never hit by this chapter's trace
}

int RouteResolver::ground_truth(int barcode_key) const noexcept {
    if (barcode_key < 0 || static_cast<std::size_t>(barcode_key) >= entries_.size()) {
        return -1;
    }
    return entries_[static_cast<std::size_t>(barcode_key)].chute;
}

void RouteResolver::reroute(const std::vector<int>& keys) {
    constexpr int reroute_chute_count = 16;  // must match manifest_chute_for_key's range
    for (int key : keys) {
        if (key < 0 || static_cast<std::size_t>(key) >= entries_.size()) {
            continue;
        }
        RouteEntry& entry = entries_[static_cast<std::size_t>(key)];
        entry.chute = reroute_chute_count + (entry.chute % reroute_chute_count);
    }
}

}  // namespace ch07
