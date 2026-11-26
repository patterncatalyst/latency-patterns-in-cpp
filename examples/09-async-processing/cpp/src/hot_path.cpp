#include "ch09/hot_path.hpp"

#include <algorithm>
#include <format>
#include <string>

namespace ch09 {

AuditRecord build_audit_record(const Parcel& parcel) {
    AuditRecord record;
    record.parcel_id = parcel.parcel_id;
    record.chute_id = parcel.chute_id;
    record.decided_at_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    const std::string label = std::format("parcel={} chute={}", parcel.parcel_id, parcel.chute_id);
    const std::size_t copy_len = std::min(label.size(), record.label.size());
    std::ranges::copy_n(label.begin(), static_cast<std::ptrdiff_t>(copy_len), record.label.begin());
    return record;
}

double process_parcel_sync(const Parcel& parcel, SyncAuditWriter& writer) {
    const auto call_start = Clock::now();
    const AuditRecord record = build_audit_record(parcel);
    writer.write_record(record);
    return std::chrono::duration<double, std::micro>(Clock::now() - call_start).count();
}

}  // namespace ch09
