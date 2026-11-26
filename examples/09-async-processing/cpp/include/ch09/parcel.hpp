#pragma once

#include <cstdint>
#include <vector>

namespace ch09 {

// The hot-path input: a parcel that already has its divert chute decided
// (the sort decision itself is "Exploiting concurrency"'s subject, not
// this chapter's). What this chapter's hot path still has to do per
// parcel is turn this into an AuditRecord and get that record durable,
// which is the work this chapter moves off the hot path.
struct Parcel {
    std::uint64_t parcel_id = 0;
    std::uint32_t chute_id = 0;
};

// Deterministic parcel generation from a seed, the same convention every
// prior chapter's example uses: every lane classifies the identical batch.
std::vector<Parcel> generate_parcels(std::size_t count, std::uint32_t chute_count, std::uint64_t seed);

}  // namespace ch09
