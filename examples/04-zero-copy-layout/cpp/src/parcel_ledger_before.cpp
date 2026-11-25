#include "ch04/parcel_ledger_before.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <string_view>

namespace ch04 {

namespace {
// Fills a cold field with deterministic, non-empty content so the struct's
// width is real rather than an artifact of leaving every byte zeroed.
// std::format rather than snprintf: type-safe, no C-style vararg interface.
void fill_blob(std::array<char, 64>& blob, std::string_view tag, std::uint64_t barcode) {
    const std::string text = std::format("{}-{:020}", tag, barcode);
    const std::size_t copy_len = std::min(text.size(), blob.size() - 1);
    std::ranges::copy_n(text.begin(), static_cast<std::ptrdiff_t>(copy_len), blob.begin());
    blob[copy_len] = '\0';
}
}  // namespace

ParcelRecordBefore::ParcelRecordBefore(std::uint64_t barcode_in, int scan_key_in,
                                        double weight_kg_in)
    : barcode(barcode_in), scan_key(scan_key_in), weight_kg(weight_kg_in) {
    fill_blob(origin_label, "origin", barcode_in);
    fill_blob(scan_notes, "notes", barcode_in);
    fill_blob(customer_ref, "cust", barcode_in);
}

ParcelLedgerBefore::ParcelLedgerBefore(std::size_t capacity) : capacity_(capacity) {
    ring_.resize(capacity_);
}

bool ParcelLedgerBefore::contains_scan_key(int scan_key) const {
    for (std::size_t i = 0; i < count_; ++i) {
        const std::unique_ptr<ParcelRecordBefore>& record_ptr = ring_[i];
        // The antipattern this lane pays for: copy the whole 200-plus-byte
        // record out of its heap slot before reading the four bytes the
        // check actually needs, instead of reading record_ptr->scan_key
        // directly.
        ParcelRecordBefore record = *record_ptr;
        if (record.scan_key == scan_key) {
            return true;
        }
    }
    return false;
}

void ParcelLedgerBefore::push(std::uint64_t barcode, int scan_key, double weight_kg) {
    ring_[next_] = std::make_unique<ParcelRecordBefore>(barcode, scan_key, weight_kg);
    next_ = (next_ + 1) % capacity_;
    if (count_ < capacity_) {
        ++count_;
    }
}

}  // namespace ch04
