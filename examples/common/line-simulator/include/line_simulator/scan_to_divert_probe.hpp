#pragma once

#include "line_simulator/parcel.hpp"

namespace line_simulator {

// The spine measurement point (see CLAUDE.md, "The spine"): scan-to-divert,
// sense -> decide -> act. A chapter's harness implements this interface to
// record timestamps; LineSimulator calls it from the scan stage's work and
// never interprets the timestamps itself. Percentiles, histograms, and
// coordinated-omission correction live in examples/_lib, not here.
//
// A pure-virtual seam on purpose: it is the mockable interface the
// line-simulator's own tests exercise with GoogleMock, and the interface a
// future chapter's real measurement harness implements.
class ScanToDivertProbe {
public:
    ScanToDivertProbe() = default;
    virtual ~ScanToDivertProbe() = default;
    ScanToDivertProbe(const ScanToDivertProbe&) = default;
    ScanToDivertProbe& operator=(const ScanToDivertProbe&) = default;
    ScanToDivertProbe(ScanToDivertProbe&&) = default;
    ScanToDivertProbe& operator=(ScanToDivertProbe&&) = default;

    // The scanner reads the barcode.
    virtual void on_sense(const Parcel& parcel, Clock::time_point at) = 0;
    // The routing lookup resolves a chute.
    virtual void on_decide(const Parcel& parcel, Clock::time_point at) = 0;
    // The diverter arm fires.
    virtual void on_act(const Parcel& parcel, Clock::time_point at) = 0;
};

}  // namespace line_simulator
