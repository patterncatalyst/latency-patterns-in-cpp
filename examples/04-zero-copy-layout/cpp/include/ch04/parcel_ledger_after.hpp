#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "ch04/parcel_ledger_before.hpp"  // key_space, divert_chute_count, review_chute, ledger_window

namespace ch04 {

// The cold fields ParcelRecordBefore interleaves with its hot ones, moved
// into their own flat, contiguous store. The duplicate-scan loop below never
// touches this type; it exists only so the after lane keeps the same total
// information as the before lane; nothing has been deleted to make the
// after lane look faster.
struct ColdRecord {
    std::array<char, 64> origin_label{};
    std::array<char, 64> scan_notes{};
    std::array<char, 64> customer_ref{};
};

// The "after" recent-scan ledger: hot fields packed into their own
// contiguous arrays (a struct-of-arrays split of exactly two columns,
// scan_key and chute), cold fields in a separate contiguous store the hot
// loop never visits. contains_scan_key borrows a std::span view over the
// live slice of scan_keys_ instead of copying anything, and walks it in one
// direction with no pointer indirection between entries.
class ParcelLedgerAfter {
public:
    explicit ParcelLedgerAfter(std::size_t capacity = ledger_window);

    [[nodiscard]] bool contains_scan_key(int scan_key) const;

    void push(std::uint64_t barcode, int scan_key, double weight_kg);

    [[nodiscard]] std::size_t size() const noexcept { return count_; }

    // Exposed for the correctness test: a read-only view of the currently
    // live scan keys, in the same arbitrary (ring) order
    // ParcelLedgerBefore::contains_scan_key walks.
    [[nodiscard]] std::span<const int> scan_keys() const noexcept {
        return {scan_keys_.data(), count_};
    }

private:
    std::vector<int> scan_keys_;    // hot, contiguous: the only field the
                                     // duplicate check reads
    std::vector<ColdRecord> cold_;  // cold, contiguous but never read here
    std::size_t capacity_;
    std::size_t next_ = 0;
    std::size_t count_ = 0;
};

}  // namespace ch04
