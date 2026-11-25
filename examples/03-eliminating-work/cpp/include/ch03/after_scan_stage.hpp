#pragma once

#include <array>
#include <memory>

#include "ch03/divert_manifest.hpp"
#include "line_simulator/parcel.hpp"
#include "line_simulator/scan_to_divert_probe.hpp"
#include "line_simulator/stage.hpp"

namespace ch03 {

// The "after" scan decode path: the same decode and the same divert
// decision as BeforeScanStage, with all three eliminated wastes gone.
//
//   1. No per-parcel allocation: the decode key is used directly, with no
//      byte-buffer round trip.
//   2. divert_table_, a std::array<int, key_space> built once in the
//      constructor, resolves the chute with one direct index.
//   3. zone_multipliers_, a std::array<double, chute_count>, is also built
//      once in the constructor instead of once per parcel.
//
// See tests/before_after_divert_test.cpp for the property test proving
// this produces the identical divert decision BeforeScanStage does, for
// every barcode either lane sees.
class AfterScanStage {
public:
    explicit AfterScanStage(std::shared_ptr<line_simulator::ScanToDivertProbe> probe);

    void operator()(line_simulator::Parcel& parcel, line_simulator::Stage stage) const;

private:
    std::shared_ptr<line_simulator::ScanToDivertProbe> probe_;
    std::array<int, key_space> divert_table_;
    std::array<double, chute_count> zone_multipliers_{};
};

}  // namespace ch03
