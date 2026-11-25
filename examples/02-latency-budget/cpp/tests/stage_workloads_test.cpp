#include "ch02/stage_workloads.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>

#include "line_simulator/parcel.hpp"

namespace ch02 {
namespace {

using line_simulator::Parcel;

Parcel sample_parcel() {
    return Parcel{
        .id = 1,
        .barcode = 4242424242ULL,
        .weight_kg = 5.0,
        .length_cm = 30.0,
        .width_cm = 20.0,
        .height_cm = 10.0,
        .chute = -1,
        .scheduled_arrival = {},
    };
}

TEST(CheckweighWork, StaysWithinTheConfiguredNoiseBandOfTheTrueWeight) {
    Parcel parcel = sample_parcel();
    const double original_weight = parcel.weight_kg;
    checkweigh_work(parcel);
    // The averaged reading is a convex combination of noisy samples, each
    // within +/-0.05kg of the true weight, so the average cannot land
    // outside that same band.
    EXPECT_LE(std::abs(parcel.weight_kg - original_weight), 0.05);
}

TEST(DimensionWork, StaysWithinTheConfiguredNoiseBandOnEveryAxis) {
    Parcel parcel = sample_parcel();
    const double original_length = parcel.length_cm;
    const double original_width = parcel.width_cm;
    const double original_height = parcel.height_cm;
    dimension_work(parcel);
    EXPECT_LE(std::abs(parcel.length_cm - original_length), 0.1);
    EXPECT_LE(std::abs(parcel.width_cm - original_width), 0.1);
    EXPECT_LE(std::abs(parcel.height_cm - original_height), 0.1);
}

TEST(ScanWork, AssignsAChuteInRange) {
    Parcel parcel = sample_parcel();
    scan_work(parcel);
    EXPECT_GE(parcel.chute, 0);
    EXPECT_LT(parcel.chute, chute_count);
}

TEST(ScanWork, IsDeterministicForTheSameBarcode) {
    Parcel first = sample_parcel();
    Parcel second = sample_parcel();
    scan_work(first);
    scan_work(second);
    EXPECT_EQ(first.chute, second.chute);
}

TEST(LabelRenderer, RendersTheBarcodeAndTheAssignedChute) {
    Parcel parcel = sample_parcel();
    parcel.chute = 7;
    LabelRenderer render;
    render(parcel);
    const std::string& line = render.last_rendered();
    EXPECT_NE(line.find("4242424242"), std::string::npos);
    EXPECT_NE(line.find("CHUTE:7"), std::string::npos);
}

TEST(SortDispatcher, CountsOneParcelPerAssignedChute) {
    SortDispatcher dispatcher{chute_count};
    Parcel first = sample_parcel();
    first.chute = 3;
    Parcel second = sample_parcel();
    second.chute = 3;
    Parcel third = sample_parcel();
    third.chute = 5;

    dispatcher(first);
    dispatcher(second);
    dispatcher(third);

    EXPECT_EQ(dispatcher.chute_counts()[3], 2U);
    EXPECT_EQ(dispatcher.chute_counts()[5], 1U);
}

TEST(SortDispatcher, IgnoresAnOutOfRangeChuteRatherThanCrashing) {
    SortDispatcher dispatcher{chute_count};
    Parcel unassigned = sample_parcel();
    unassigned.chute = -1;
    dispatcher(unassigned);  // must not throw or write out of bounds

    Parcel oversize = sample_parcel();
    oversize.chute = chute_count + 1;
    dispatcher(oversize);  // same: out of range, silently ignored

    for (const std::uint64_t count : dispatcher.chute_counts()) {
        EXPECT_EQ(count, 0U);
    }
}

}  // namespace
}  // namespace ch02
