#include "ch03/before_scan_stage.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ch03 {

namespace {
constexpr std::size_t record_size_bytes = 64;  // a realistic small decode record
constexpr double volumetric_divisor_cm3_per_kg = 5000.0;  // courier-industry volumetric-weight formula
constexpr double oversize_margin = 1.5;  // volumetric weight > 1.5x actual weight
}  // namespace

BeforeScanStage::BeforeScanStage(std::shared_ptr<line_simulator::ScanToDivertProbe> probe)
    : probe_(std::move(probe)), manifest_map_(build_manifest_map()) {}

void BeforeScanStage::operator()(line_simulator::Parcel& parcel, line_simulator::Stage stage) const {
    using line_simulator::Clock;
    using line_simulator::Stage;
    if (stage != Stage::Scan) {
        return;  // every other stage keeps the house default (a no-op)
    }

    if (probe_) {
        probe_->on_sense(parcel, Clock::now());
    }

    const int decode_key = decode_key_for_barcode(parcel.barcode);

    // Sin #1: a per-parcel heap allocation to round-trip the decode key
    // through a byte buffer. Nothing downstream needs this record in byte
    // form; it exists only because this decode path was written as
    // "decode, then serialize, then look up" instead of "decode, then look
    // up" (see the chapter prose, "Serializing and deserializing cost").
    const auto decode_key_bits = static_cast<std::uint32_t>(decode_key);
    std::vector<std::byte> record(record_size_bytes);
    record[0] = static_cast<std::byte>(decode_key_bits & 0xFFU);
    record[1] = static_cast<std::byte>((decode_key_bits >> 8U) & 0xFFU);
    record[2] = static_cast<std::byte>((decode_key_bits >> 16U) & 0xFFU);
    record[3] = static_cast<std::byte>((decode_key_bits >> 24U) & 0xFFU);
    const auto lookup_key_bits = static_cast<std::uint32_t>(std::to_integer<unsigned int>(record[0])) |
                                  (static_cast<std::uint32_t>(std::to_integer<unsigned int>(record[1])) << 8U) |
                                  (static_cast<std::uint32_t>(std::to_integer<unsigned int>(record[2])) << 16U) |
                                  (static_cast<std::uint32_t>(std::to_integer<unsigned int>(record[3])) << 24U);
    const int lookup_key = static_cast<int>(lookup_key_bits);

    // Sin #2: a hash-map lookup to resolve the chute, where a dense array
    // (see AfterScanStage) could do one direct index instead (see the
    // chapter prose, "Algorithmic complexity").
    int chute = manifest_map_.at(lookup_key);

    // Sin #3: rebuilding the zone-multiplier table from scratch on every
    // parcel, even though manifest_zone_multiplier_for_chute's output never
    // changes across a run (see the chapter prose, "Precomputation").
    std::vector<double> zone_multipliers(chute_count);
    for (int i = 0; i < chute_count; ++i) {
        zone_multipliers[static_cast<std::size_t>(i)] = manifest_zone_multiplier_for_chute(i);
    }

    const double volumetric_weight_kg =
        (parcel.length_cm * parcel.width_cm * parcel.height_cm) / volumetric_divisor_cm3_per_kg;
    const bool oversize_for_weight =
        volumetric_weight_kg * zone_multipliers[static_cast<std::size_t>(chute)] >
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
