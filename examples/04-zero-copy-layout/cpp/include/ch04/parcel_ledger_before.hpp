#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace ch04 {

// Dense key space for the recent-scan ledger, chosen so a window of
// ledger_window records collides with a new parcel's key often enough to
// exercise the duplicate-scan path on a real run (birthday-problem math,
// not tuning for a specific answer): see the chapter prose, "The before
// path and why it is cache-hostile".
inline constexpr int key_space = 701;
inline constexpr int divert_chute_count = 12;
// A 13th, dedicated chute for a parcel whose scan key matches one already in
// the recent-scan window: a misfeed or a duplicate read, routed to manual
// review instead of its normal chute.
inline constexpr int review_chute = divert_chute_count;
inline constexpr std::size_t ledger_window = 256;

// One ledger entry, the realistic shape a record grows into when a hot field
// (scan_key) and a result field (chute) sit right next to fields the scan
// loop never reads: a human-facing origin label, a free-text note left by an
// earlier handling exception, and a customer reference. None of the three
// cold fields is a pointer or a heap buffer (Chapter 3 already measured what
// a per-parcel heap allocation costs); each is a fixed-size inline blob, so
// this struct's only new cost is its width, not what allocates it.
struct ParcelRecordBefore {
    std::uint64_t barcode = 0;              // hot: identity
    std::array<char, 64> origin_label{};    // cold
    int scan_key = 0;                       // hot: what the duplicate check reads
    std::array<char, 64> scan_notes{};      // cold
    double weight_kg = 0.0;                 // not read by the duplicate check
    std::array<char, 64> customer_ref{};    // cold
    int chute = -1;                         // hot: the result

    ParcelRecordBefore() = default;
    ParcelRecordBefore(std::uint64_t barcode_in, int scan_key_in, double weight_kg_in);
};

// The "before" recent-scan ledger: a fixed-capacity ring of
// std::unique_ptr<ParcelRecordBefore>, one heap allocation per entry. This is
// the array-of-structs-of-pointers shape many languages default to (a Java
// `ArrayList<Record>` or a Python list of objects): the ring itself is
// contiguous, but what it holds is 256 independent addresses the allocator
// handed out on its own schedule, so walking the ring chases a different,
// unrelated cache line on every step. See ParcelLedgerAfter for the
// contiguous alternative.
class ParcelLedgerBefore {
public:
    explicit ParcelLedgerBefore(std::size_t capacity = ledger_window);

    // True if any record currently in the ring carries this scan_key. Reads
    // every live entry by copying the whole record out of its heap slot
    // first ("record = *record_ptr") instead of reading the one field it
    // needs through the pointer: the in-process copy this chapter's before
    // lane pays for on top of the pointer chase itself.
    [[nodiscard]] bool contains_scan_key(int scan_key) const;

    // Appends a new record, evicting the oldest once the ring is full.
    void push(std::uint64_t barcode, int scan_key, double weight_kg);

    [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
    std::vector<std::unique_ptr<ParcelRecordBefore>> ring_;
    std::size_t capacity_;
    std::size_t next_ = 0;
    std::size_t count_ = 0;
};

}  // namespace ch04
