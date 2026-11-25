#include "ch04/layout_scan_stage.hpp"

#include "line_simulator/parcel.hpp"

namespace ch04 {

namespace {
constexpr std::uint64_t scan_key_multiplier = 2654435761ULL;  // Knuth's multiplicative hash
constexpr unsigned int scan_key_shift = 13;
}  // namespace

int scan_key_for_barcode(std::uint64_t barcode) noexcept {
    std::uint64_t checksum = barcode;
    checksum = (checksum * scan_key_multiplier) ^ (checksum >> scan_key_shift);
    return static_cast<int>(checksum % static_cast<std::uint64_t>(key_space));
}

BeforeLayoutScanStage::BeforeLayoutScanStage(std::shared_ptr<line_simulator::ScanToDivertProbe> probe)
    : probe_(std::move(probe)), ledger_(ledger_window) {}

void BeforeLayoutScanStage::operator()(line_simulator::Parcel& parcel, line_simulator::Stage stage) {
    if (stage != line_simulator::Stage::Scan) {
        return;
    }
    if (probe_) {
        probe_->on_sense(parcel, line_simulator::Clock::now());
    }

    const int scan_key = scan_key_for_barcode(parcel.barcode);
    const bool duplicate = ledger_.contains_scan_key(scan_key);
    parcel.chute = duplicate ? review_chute : (scan_key % divert_chute_count);
    if (duplicate) {
        ++review_count_;
    }
    ledger_.push(parcel.barcode, scan_key, parcel.weight_kg);

    if (probe_) {
        probe_->on_decide(parcel, line_simulator::Clock::now());
        probe_->on_act(parcel, line_simulator::Clock::now());
    }
}

AfterLayoutScanStage::AfterLayoutScanStage(std::shared_ptr<line_simulator::ScanToDivertProbe> probe)
    : probe_(std::move(probe)), ledger_(ledger_window) {}

void AfterLayoutScanStage::operator()(line_simulator::Parcel& parcel, line_simulator::Stage stage) {
    if (stage != line_simulator::Stage::Scan) {
        return;
    }
    if (probe_) {
        probe_->on_sense(parcel, line_simulator::Clock::now());
    }

    const int scan_key = scan_key_for_barcode(parcel.barcode);
    const bool duplicate = ledger_.contains_scan_key(scan_key);
    parcel.chute = duplicate ? review_chute : (scan_key % divert_chute_count);
    if (duplicate) {
        ++review_count_;
    }
    ledger_.push(parcel.barcode, scan_key, parcel.weight_kg);

    if (probe_) {
        probe_->on_decide(parcel, line_simulator::Clock::now());
        probe_->on_act(parcel, line_simulator::Clock::now());
    }
}

}  // namespace ch04
