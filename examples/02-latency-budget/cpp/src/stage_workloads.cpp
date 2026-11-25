#include "ch02/stage_workloads.hpp"

#include <iomanip>
#include <sstream>

namespace ch02 {

namespace {
constexpr std::uint64_t hash_multiplier = 2654435761ULL;  // Knuth's multiplicative hash
constexpr unsigned int hash_shift = 13;

constexpr int checkweigh_sample_count = 8;
constexpr double checkweigh_noise_scale_kg = 0.05;

constexpr int dimension_sweep_count = 6;
constexpr double dimension_noise_scale_cm = 0.1;

constexpr int scan_decode_iterations = 4000;

// Advances a cheap, deterministic noise generator and returns a value in
// [-1.0, 1.0), the same multiplicative-hash shape Chapter 1's decode cost
// uses (see ch01::LoadedScanStage), reused here to stand in for sensor
// noise instead of a barcode checksum.
double next_unit_noise(std::uint64_t& state) {
    state = (state * hash_multiplier) ^ (state >> hash_shift);
    return (static_cast<double>(state % 2001) / 1000.0) - 1.0;
}

double average_noisy_readings(double true_value, double noise_scale, int sample_count,
                               std::uint64_t& noise_state) {
    double sum = 0.0;
    for (int i = 0; i < sample_count; ++i) {
        sum += true_value + (next_unit_noise(noise_state) * noise_scale);
    }
    return sum / static_cast<double>(sample_count);
}

}  // namespace

void checkweigh_work(line_simulator::Parcel& parcel) {
    std::uint64_t noise_state = parcel.barcode;
    parcel.weight_kg = average_noisy_readings(parcel.weight_kg, checkweigh_noise_scale_kg,
                                               checkweigh_sample_count, noise_state);
}

void dimension_work(line_simulator::Parcel& parcel) {
    std::uint64_t noise_state = parcel.barcode ^ 0x9E3779B97F4A7C15ULL;
    parcel.length_cm = average_noisy_readings(parcel.length_cm, dimension_noise_scale_cm,
                                               dimension_sweep_count, noise_state);
    parcel.width_cm = average_noisy_readings(parcel.width_cm, dimension_noise_scale_cm,
                                              dimension_sweep_count, noise_state);
    parcel.height_cm = average_noisy_readings(parcel.height_cm, dimension_noise_scale_cm,
                                               dimension_sweep_count, noise_state);
}

void scan_work(line_simulator::Parcel& parcel) {
    std::uint64_t checksum = parcel.barcode;
    for (int i = 0; i < scan_decode_iterations; ++i) {
        checksum = (checksum * hash_multiplier) ^ (checksum >> hash_shift);
    }
    parcel.chute = static_cast<int>(checksum % static_cast<std::uint64_t>(chute_count));
}

void LabelRenderer::operator()(line_simulator::Parcel& parcel) {
    std::ostringstream line;
    line << "BARCODE:" << parcel.barcode << " CHUTE:" << parcel.chute << " WEIGHT_KG:" << std::fixed
         << std::setprecision(3) << parcel.weight_kg << " DIM_CM:" << parcel.length_cm << "x"
         << parcel.width_cm << "x" << parcel.height_cm;
    last_rendered_ = line.str();
}

SortDispatcher::SortDispatcher(int num_chutes) : chute_counts_(static_cast<std::size_t>(num_chutes), 0) {}

void SortDispatcher::operator()(line_simulator::Parcel& parcel) {
    if (parcel.chute < 0 || static_cast<std::size_t>(parcel.chute) >= chute_counts_.size()) {
        return;  // an unassigned or out-of-range chute has nothing to count
    }
    ++chute_counts_[static_cast<std::size_t>(parcel.chute)];
}

StageWorkloads make_stage_workloads(LabelRenderer& label, SortDispatcher& sort) {
    return StageWorkloads{
        .checkweigh = checkweigh_work,
        .dimension = dimension_work,
        .scan = scan_work,
        .label = [&label](line_simulator::Parcel& parcel) { label(parcel); },
        .sort = [&sort](line_simulator::Parcel& parcel) { sort(parcel); },
    };
}

}  // namespace ch02
