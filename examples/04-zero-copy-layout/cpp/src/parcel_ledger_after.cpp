#include "ch04/parcel_ledger_after.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

namespace ch04 {

namespace {
// Same std::format-based fill as ParcelLedgerBefore's cold fields (see
// parcel_ledger_before.cpp): kept duplicated rather than shared, since the
// two types otherwise share no code and this chapter's point is the layout
// difference between them, not a common base.
void fill_blob(std::array<char, 64>& blob, std::string_view tag, std::uint64_t barcode) {
    const std::string text = std::format("{}-{:020}", tag, barcode);
    const std::size_t copy_len = std::min(text.size(), blob.size() - 1);
    std::ranges::copy_n(text.begin(), static_cast<std::ptrdiff_t>(copy_len), blob.begin());
    blob[copy_len] = '\0';
}
}  // namespace

ParcelLedgerAfter::ParcelLedgerAfter(std::size_t capacity) : capacity_(capacity) {
    scan_keys_.resize(capacity_);
    cold_.resize(capacity_);
}

bool ParcelLedgerAfter::contains_scan_key(int scan_key) const {
    const std::span<const int> keys(scan_keys_.data(), count_);
    return std::ranges::any_of(keys, [scan_key](int key) { return key == scan_key; });
}

void ParcelLedgerAfter::push(std::uint64_t barcode, int scan_key, double /*weight_kg*/) {
    scan_keys_[next_] = scan_key;
    ColdRecord& cold = cold_[next_];
    fill_blob(cold.origin_label, "origin", barcode);
    fill_blob(cold.scan_notes, "notes", barcode);
    fill_blob(cold.customer_ref, "cust", barcode);
    next_ = (next_ + 1) % capacity_;
    if (count_ < capacity_) {
        ++count_;
    }
}

}  // namespace ch04
