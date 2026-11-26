#include "ch09/async_trial.hpp"

namespace ch09 {

AsyncTrialResult run_sync_inline_trial(const std::vector<Parcel>& parcels, const std::filesystem::path& audit_path) {
    AsyncTrialResult result;
    result.completion_us.reserve(parcels.size());

    SyncAuditWriter writer(audit_path);
    const auto trial_start = Clock::now();
    for (const Parcel& parcel : parcels) {
        result.completion_us.push_back(process_parcel_sync(parcel, writer));
    }
    result.total_wall_time_us = std::chrono::duration<double, std::micro>(Clock::now() - trial_start).count();
    return result;
}

}  // namespace ch09
