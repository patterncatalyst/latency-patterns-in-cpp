#include "ch09/parcel.hpp"

#include <random>

namespace ch09 {

std::vector<Parcel> generate_parcels(std::size_t count, std::uint32_t chute_count, std::uint64_t seed) {
    std::vector<Parcel> parcels;
    parcels.reserve(count);
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::uint32_t> chute_dist(0, chute_count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        Parcel parcel;
        parcel.parcel_id = static_cast<std::uint64_t>(i);
        parcel.chute_id = chute_dist(rng);
        parcels.push_back(parcel);
    }
    return parcels;
}

}  // namespace ch09
