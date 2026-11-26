#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ch09 {

// The off-path observability hook: the background consumer calls
// record_batch_flush() after every batch it flushes, never the hot path
// (there is nothing to call from the hot path; by the time a parcel's
// record is in the queue, the hot path has already returned). With no
// endpoint configured this is a no-op; with CH09_OTLP_ENDPOINT set (see
// demo.sh and the chapter's "Off-path observability" section), it posts two
// OTLP/HTTP JSON metrics, batch size and flush duration, to an OpenTelemetry
// Collector such as the one examples/09-async-processing/observability/
// documents standing up via lgtm-docker-stack. A send failure (collector
// not running, endpoint unreachable) is swallowed here, not thrown: a
// telemetry sink that can take down the consumer it instruments would move
// the exact failure mode this chapter exists to remove one layer deeper
// instead of removing it.
class TelemetrySink {
public:
    // endpoint is "host:port" (the collector's OTLP/HTTP port, e.g.
    // "127.0.0.1:4318"); an empty string disables export entirely.
    explicit TelemetrySink(const std::string& otlp_http_endpoint);

    void record_batch_flush(std::size_t batch_size, double flush_duration_us) noexcept;

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] std::uint64_t export_attempt_count() const noexcept { return export_attempt_count_; }
    [[nodiscard]] std::uint64_t export_failure_count() const noexcept { return export_failure_count_; }

private:
    bool post_otlp_metrics(std::size_t batch_size, double flush_duration_us) noexcept;

    std::string host_;
    std::uint16_t port_ = 0;
    bool enabled_ = false;
    std::uint64_t export_attempt_count_ = 0;
    std::uint64_t export_failure_count_ = 0;
};

}  // namespace ch09
