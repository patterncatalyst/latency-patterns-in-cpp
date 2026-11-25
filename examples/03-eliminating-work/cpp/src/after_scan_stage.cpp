#include "ch03/after_scan_stage.hpp"

#include <cstddef>

namespace ch03 {

namespace {
constexpr double volumetric_divisor_cm3_per_kg = 5000.0;  // courier-industry volumetric-weight formula
constexpr double oversize_margin = 1.5;  // volumetric weight > 1.5x actual weight
}  // namespace

AfterScanStage::AfterScanStage(std::shared_ptr<line_simulator::ScanToDivertProbe> probe)
    : probe_(std::move(probe)), divert_table_(build_manifest_table()) {
    for (int i = 0; i < chute_count; ++i) {
        zone_multipliers_[static_cast<std::size_t>(i)] = manifest_zone_multiplier_for_chute(i);
    }
}

void AfterScanStage::operator()(line_simulator::Parcel& parcel, line_simulator::Stage stage) const {
    using line_simulator::Clock;
    using line_simulator::Stage;
    if (stage != Stage::Scan) {
        return;  // every other stage keeps the house default (a no-op)
    }

    if (probe_) {
        probe_->on_sense(parcel, Clock::now());
    }

    const int decode_key = decode_key_for_barcode(parcel.barcode);

    // Fixed #1 and #2: no byte-buffer round trip, and one direct array
    // index instead of a hash-map lookup.
    int chute = divert_table_[static_cast<std::size_t>(decode_key)];

    // Fixed #3: zone_multipliers_ was built once, in the constructor, not
    // rebuilt on every parcel.
    const double volumetric_weight_kg =
        (parcel.length_cm * parcel.width_cm * parcel.height_cm) / volumetric_divisor_cm3_per_kg;
    const bool oversize_for_weight =
        volumetric_weight_kg * zone_multipliers_[static_cast<std::size_t>(chute)] >
        parcel.weight_kg * oversize_margin;
    if (oversize_for_weight) {
        chute = oversize_chute;
    }

    parcel.chute = chute;

    if (probe_) {
        probe_->on_decide(parcel, Clock::now());
    }
    if (probe_) {
        probe_->on_act(parcel, Clock::now());
    }
}

}  // namespace ch03
