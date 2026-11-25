#pragma once

#include <cstdint>
#include <memory>

#include "ch04/parcel_ledger_after.hpp"
#include "ch04/parcel_ledger_before.hpp"
#include "line_simulator/parcel.hpp"
#include "line_simulator/scan_to_divert_probe.hpp"
#include "line_simulator/stage.hpp"

namespace ch04 {

// Folds a barcode into the dense key space both ledgers key on. Identical in
// both lanes; see ch03::decode_key_for_barcode for the same shape used to a
// different end (that chapter's point was eliminating the decode's
// surrounding waste, not the decode itself; this chapter's point is what the
// ledger does with the key afterward, not the decode).
[[nodiscard]] int scan_key_for_barcode(std::uint64_t barcode) noexcept;

// The "before" scan stage: on every parcel, checks the recent-scan ledger
// for this parcel's scan_key, routes a hit to review_chute (a likely
// misfeed or duplicate read) and a miss to its normal chute, then appends
// the parcel to the ledger. Reads and writes go through ParcelLedgerBefore,
// the array-of-pointers-to-wide-structs layout (see
// parcel_ledger_before.hpp).
class BeforeLayoutScanStage {
public:
    explicit BeforeLayoutScanStage(std::shared_ptr<line_simulator::ScanToDivertProbe> probe);

    void operator()(line_simulator::Parcel& parcel, line_simulator::Stage stage);

    [[nodiscard]] std::uint64_t review_count() const noexcept { return review_count_; }

private:
    std::shared_ptr<line_simulator::ScanToDivertProbe> probe_;
    ParcelLedgerBefore ledger_;
    std::uint64_t review_count_ = 0;
};

// The "after" scan stage: the identical decision from the identical inputs
// (see tests/layout_scan_stage_test.cpp), reading and writing through
// ParcelLedgerAfter, the struct-of-arrays layout.
class AfterLayoutScanStage {
public:
    explicit AfterLayoutScanStage(std::shared_ptr<line_simulator::ScanToDivertProbe> probe);

    void operator()(line_simulator::Parcel& parcel, line_simulator::Stage stage);

    [[nodiscard]] std::uint64_t review_count() const noexcept { return review_count_; }

private:
    std::shared_ptr<line_simulator::ScanToDivertProbe> probe_;
    ParcelLedgerAfter ledger_;
    std::uint64_t review_count_ = 0;
};

}  // namespace ch04
