#pragma once

#include <memory>
#include <unordered_map>

#include "ch03/divert_manifest.hpp"
#include "line_simulator/parcel.hpp"
#include "line_simulator/scan_to_divert_probe.hpp"
#include "line_simulator/stage.hpp"

namespace ch03 {

// The "before" scan decode path: the realistic, unoptimized shape this
// chapter targets. On every parcel, after the (shared, unchanged) barcode
// decode, it does three pieces of work this chapter eliminates:
//
//   1. A heap allocation to round-trip the decode key through a byte
//      buffer, standing in for a decode path that serializes its result
//      into a record it does not need before using it.
//   2. A std::unordered_map lookup to resolve the chute, where a dense
//      array could do one direct index instead.
//   3. A from-scratch rebuild of the zone-multiplier table, even though
//      its values never change across a run.
//
// AfterScanStage produces the SAME divert decision from the same inputs
// (see tests/before_after_divert_test.cpp); only the cost of getting there
// differs. Installed via LineSimulator::set_stage_work(Stage::Scan, ...),
// the same pattern ch01::LoadedScanStage uses: overriding a stage skips the
// simulator's own default probe calls, so this type drives sense/decide/act
// itself.
class BeforeScanStage {
public:
    explicit BeforeScanStage(std::shared_ptr<line_simulator::ScanToDivertProbe> probe);

    void operator()(line_simulator::Parcel& parcel, line_simulator::Stage stage) const;

private:
    std::shared_ptr<line_simulator::ScanToDivertProbe> probe_;
    std::unordered_map<int, int> manifest_map_;
};

}  // namespace ch03
