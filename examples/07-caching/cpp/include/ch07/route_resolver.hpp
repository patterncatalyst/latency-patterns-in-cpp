#pragma once

#include <cstdint>
#include <vector>

namespace ch07 {

// The barcode-to-route manifest: for each key in [0, key_space), which chute
// that SKU family currently routes to. A real sortation line keeps this in a
// backing table or a service call away, not in a map a scan thread already
// holds; this type stands in for that backing source.
struct RouteEntry {
    int key;
    int chute;
};

// This chapter's slow backing lookup: a linear scan over `entries_`, standing
// in for a disk-backed table or a service hop a cache sits in front of to
// avoid paying twice. `resolve()` is the one function every lane in this
// chapter times: the uncached baseline calls it on every access, and every
// cached lane calls it only on a miss.
//
// `resolve()` does two real things on every call, never a sleep: it walks
// every entry up to and including the matching one (an O(n) scan where a
// real index would do better, the cost this chapter's cache exists to hide),
// and it folds each scanned entry's key into a running checksum, standing in
// for the per-row verification work a real backing scan pays (confirming a
// row matches, decoding a sector, deserializing a response). The checksum is
// stored, not discarded, so the compiler cannot prove the scan has no effect
// and optimize it away.
class RouteResolver {
public:
    // Builds `key_space` entries, keys 0..key_space-1 in that order (so a
    // given key's scan length is fixed by its position, not by whether it is
    // popular; see ZipfTrace for why popularity is shuffled independently of
    // key value). Each key's initial chute comes from a fixed, deterministic
    // assignment, not a real manifest: see manifest_chute_for_key().
    explicit RouteResolver(int key_space);

    // The slow path every lane measures: walk entries_ from the front,
    // folding each scanned key into a checksum, and return the chute of the
    // first (and only) entry whose key matches.
    [[nodiscard]] int resolve(int barcode_key) const;

    // The checksum resolve() left behind on its most recent call, exposed so
    // a test can confirm the scan actually ran its full length rather than
    // being optimized into a direct lookup.
    [[nodiscard]] std::uint64_t last_scan_checksum() const noexcept { return scan_checksum_; }

    // O(1) direct read of the CURRENT chute for a key, used only to check
    // whether a cache's served answer is stale after reroute(); never on the
    // path any lane's latency is measured on.
    [[nodiscard]] int ground_truth(int barcode_key) const noexcept;

    // Simulates a re-route event: reassigns every key in `keys` to a new
    // chute, one past the end of the original chute range, so the change is
    // unambiguous. Models a label reassignment arriving after a cache has
    // already served (and may still be holding) the old answer.
    void reroute(const std::vector<int>& keys);

    [[nodiscard]] int key_space() const noexcept { return static_cast<int>(entries_.size()); }

private:
    std::vector<RouteEntry> entries_;
    mutable std::uint64_t scan_checksum_ = 0;
};

// A fixed, deterministic (but otherwise arbitrary) chute assignment for a
// dense key, standing in for a real sortation manifest. Used only to build
// RouteResolver's initial entries_, never on a per-access path.
[[nodiscard]] int manifest_chute_for_key(int dense_key) noexcept;

}  // namespace ch07
