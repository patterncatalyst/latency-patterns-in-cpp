// examples/09-async-processing/cpp/src/main.cpp
//
// Chapter 9: the scan-to-divert hot path's audit/label write, measured
// synchronous-inline (the "before") against handed off to a background
// event-loop consumer (the "after"), plus a dedicated small-queue trial
// that forces real backpressure shedding, plus a secondary benchmark of the
// background consumer's own batched flush: a blocking writev() versus
// io_uring, if liburing resolved (see CLAUDE.md, "The spine";
// _plans/premise-probe.md for the measured effect).
//
// Usage: ch09_async_processing <results-dir> <scrub-script> [python3]
//   Same contract as every prior chapter's example.
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "ch09/async_trial.hpp"
#include "ch09/batch_audit_writer.hpp"
#include "ch09/handoff_queue_types.hpp"
#include "ch09/parcel.hpp"
#include "ch09/sync_audit_writer.hpp"
#include "ch09/telemetry_sink.hpp"
#include "measure/latency_recorder.hpp"
#include "measure/logging.hpp"
#include "measure/scrub.hpp"

#ifdef CH09_HAVE_IO_URING
#include "ch09/io_uring_batch_writer.hpp"
#endif

namespace {

namespace fs = std::filesystem;
using measure::Histogram;
using measure::LatencyRecorder;

constexpr std::size_t headline_item_count = 20'000;
constexpr std::size_t backpressure_item_count = 20'000;
constexpr std::uint32_t chute_count = 64;
constexpr std::size_t batch_capacity = 64;
constexpr auto max_batch_delay = std::chrono::milliseconds(5);
constexpr auto headline_max_wait = std::chrono::microseconds(5'000);
constexpr auto backpressure_max_wait = std::chrono::microseconds(50);
constexpr std::size_t flush_benchmark_batch_count = 2'000;

std::string read_profile_header(std::istream& in) {
    std::ostringstream header;
    std::string line;
    while (std::getline(in, line)) {
        header << line << '\n';
    }
    return header.str();
}

std::string render_histogram(const Histogram& hist) {
    std::ostringstream out;
    const auto max_count = *std::ranges::max_element(hist.counts);
    constexpr int bar_width = 40;
    for (std::size_t i = 0; i < hist.counts.size(); ++i) {
        const double bucket_start_us = (hist.min_value + (static_cast<double>(i) * hist.bucket_width)) * 1e6;
        const double bucket_end_us = bucket_start_us + (hist.bucket_width * 1e6);
        const auto filled = max_count == 0 ? 0 : static_cast<int>(hist.counts[i] * bar_width / max_count);
        out << std::fixed << std::setprecision(3) << std::setw(10) << bucket_start_us << " us - " << std::setw(10)
            << bucket_end_us << " us | " << std::string(static_cast<std::size_t>(filled), '#') << ' '
            << hist.counts[i] << '\n';
    }
    return out.str();
}

LatencyRecorder recorder_from_us(const std::vector<double>& completion_us) {
    LatencyRecorder recorder;
    for (const double us : completion_us) {
        recorder.record(std::chrono::duration<double>(us * 1e-6));
    }
    return recorder;
}

void write_recorder(std::ostream& out, const std::string& key_prefix, const LatencyRecorder& recorder) {
    out << key_prefix << "_sample_count=" << recorder.sample_count() << '\n';
    if (recorder.sample_count() == 0) {
        return;
    }
    out << key_prefix << "_p50_us=" << std::fixed << std::setprecision(3) << recorder.percentile(50) * 1e6 << '\n';
    out << key_prefix << "_p99_us=" << recorder.percentile(99) * 1e6 << '\n';
    out << key_prefix << "_p999_us=" << recorder.percentile(99.9) * 1e6 << '\n';
}

void write_trial(std::ostream& out, const std::string& key_prefix, const ch09::AsyncTrialResult& trial) {
    const LatencyRecorder recorder = recorder_from_us(trial.completion_us);
    const auto item_count = static_cast<std::uint64_t>(trial.completion_us.size());
    const double throughput_items_per_sec =
        trial.total_wall_time_us > 0.0 ? static_cast<double>(item_count) / (trial.total_wall_time_us * 1e-6) : 0.0;

    out << key_prefix << "_item_count=" << item_count << '\n';
    out << key_prefix << "_wall_time_us=" << std::fixed << std::setprecision(3) << trial.total_wall_time_us << '\n';
    out << key_prefix << "_throughput_items_per_sec=" << std::setprecision(1) << throughput_items_per_sec << '\n';
    out << key_prefix << "_shed_count=" << trial.shed_count << '\n';
    out << key_prefix << "_high_water_mark=" << trial.high_water_mark << '\n';
    write_recorder(out, key_prefix, recorder);
    out << "# " << key_prefix << " completion-time histogram (microseconds since trial start, count, bar)\n";
    out << render_histogram(recorder.histogram());
}

struct RecordSetCheck {
    std::uint64_t mismatch_count = 0;
    std::uint64_t duplicate_count = 0;
};

RecordSetCheck compare_record_sets(const std::vector<ch09::Parcel>& parcels, const fs::path& audit_path) {
    const std::vector<ch09::AuditRecord> records = ch09::read_all_records(audit_path);
    std::unordered_set<std::uint64_t> expected;
    expected.reserve(parcels.size());
    for (const ch09::Parcel& parcel : parcels) {
        expected.insert(parcel.parcel_id);
    }

    RecordSetCheck check;
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(records.size());
    for (const ch09::AuditRecord& record : records) {
        if (!seen.insert(record.parcel_id).second) {
            ++check.duplicate_count;
        }
        if (!expected.contains(record.parcel_id)) {
            ++check.mismatch_count;
        }
    }
    for (const std::uint64_t parcel_id : expected) {
        if (!seen.contains(parcel_id)) {
            ++check.mismatch_count;
        }
    }
    return check;
}

LatencyRecorder benchmark_flush(ch09::BatchAuditWriter& writer, const std::vector<ch09::AuditRecord>& batch,
                                 std::size_t batch_count) {
    LatencyRecorder recorder;
    for (std::size_t i = 0; i < batch_count; ++i) {
        const auto start = std::chrono::steady_clock::now();
        writer.flush_batch(batch);
        const double duration_us =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        recorder.record(std::chrono::duration<double>(duration_us * 1e-6));
    }
    return recorder;
}

int run_chapter9(int argc, char** argv);

}  // namespace

int main(int argc, char** argv) {
    // Every lane below can throw (a failed open(), io_uring_queue_init(),
    // or epoll_create1() all surface as std::runtime_error): main() itself
    // must not let one escape uncaught, so the real work lives in
    // run_chapter9() and this wrapper is the one place that catches it.
    try {
        return run_chapter9(argc, argv);
    } catch (const std::exception& error) {
        spdlog::error("ch09_async_processing: {}", error.what());
        return EXIT_FAILURE;
    }
}

namespace {

int run_chapter9(int argc, char** argv) {
    measure::init_logging("ch09-async-processing");

    if (argc < 3) {
        spdlog::error("usage: ch09_async_processing <results-dir> <scrub-script> [python3]");
        return EXIT_FAILURE;
    }
    const fs::path results_dir = argv[1];
    const fs::path scrub_script = argv[2];
    const fs::path python3 = argc > 3 ? fs::path(argv[3]) : fs::path("python3");

    const std::string profile_header = read_profile_header(std::cin);

    fs::create_directories(results_dir);
    // .raw/ holds the per-record audit logs and the flush-benchmark's raw
    // batch files: intermediate artifacts this chapter's results/.txt
    // summarizes, not durable results in their own right (see .gitignore's
    // examples/**/results/.raw/ entry: never committed).
    fs::create_directories(results_dir / ".raw");
    measure::ResultsDirScrubGuard scrub_guard(results_dir, scrub_script, python3);

    // Off-path OTLP export is opt-in: unset (the default) means
    // TelemetrySink is constructed disabled and every record_batch_flush()
    // call is a no-op, so the headline measurement never depends on a
    // collector being up (see "Off-path observability").
    const char* otlp_endpoint_env = std::getenv("CH09_OTLP_ENDPOINT");
    ch09::TelemetrySink telemetry(otlp_endpoint_env != nullptr ? std::string(otlp_endpoint_env) : std::string());

    std::ofstream out(results_dir / "async-processing.txt", std::ios::trunc);
    out << "# Chapter 9 async-processing run: the scan-to-divert hot path's\n";
    out << "# audit/label write, measured synchronous-inline versus handed\n";
    out << "# off to a background event-loop consumer (epoll + batching +\n";
    out << "# a buffer pool + bounded backpressure), plus a dedicated small-\n";
    out << "# queue trial that forces real backpressure shedding, plus a\n";
    out << "# secondary benchmark of the background consumer's own batched\n";
    out << "# flush backend (blocking writev versus io_uring, if available).\n";
    out << profile_header;
#ifdef CH09_HAVE_IO_URING
    out << "io_uring_available=1\n";
#else
    out << "io_uring_available=0\n";
#endif
    out << "batch_capacity=" << batch_capacity << '\n';
    out << "max_batch_delay_ms=" << max_batch_delay.count() << '\n';
    out << "telemetry_otlp_enabled=" << (telemetry.enabled() ? 1 : 0) << '\n';

    // --- Lanes 1 and 2: sync-inline versus off-path, headline comparison --
    spdlog::info("generating {} parcels...", headline_item_count);
    const std::vector<ch09::Parcel> headline_parcels = ch09::generate_parcels(headline_item_count, chute_count, 0xA5A5ULL);

    spdlog::info("sync-inline lane: {} parcels, one O_DSYNC write each, on the hot path...", headline_item_count);
    const fs::path sync_audit_path = results_dir / ".raw" / "audit-sync.log";
    const ch09::AsyncTrialResult sync_result = ch09::run_sync_inline_trial(headline_parcels, sync_audit_path);

    spdlog::info("off-path lane: {} parcels, handed to the event-loop consumer, batch flush of {}...",
                 headline_item_count, batch_capacity);
    const fs::path offpath_audit_path = results_dir / ".raw" / "audit-offpath.log";
#ifdef CH09_HAVE_IO_URING
    ch09::IoUringBatchAuditWriter offpath_writer(offpath_audit_path);
    out << "offpath_writer_backend=io_uring\n";
#else
    ch09::BlockingBatchAuditWriter offpath_writer(offpath_audit_path);
    out << "offpath_writer_backend=blocking_writev\n";
#endif
    const ch09::AsyncTrialResult offpath_result = ch09::run_offpath_trial<ch09::HandoffQueue>(
        headline_parcels, offpath_writer, batch_capacity, max_batch_delay, headline_max_wait, &telemetry);

    const RecordSetCheck set_check = compare_record_sets(headline_parcels, offpath_audit_path);
    const RecordSetCheck sync_set_check = compare_record_sets(headline_parcels, sync_audit_path);

    out << "\n# --- lane 1: sync-inline (one O_DSYNC write per parcel, on the hot path) ---\n";
    write_trial(out, "sync", sync_result);
    out << "sync_vs_input_mismatch_count=" << sync_set_check.mismatch_count << '\n';
    out << "sync_duplicate_count=" << sync_set_check.duplicate_count << '\n';

    out << "\n# --- lane 2: off-path (queue capacity " << ch09::HandoffQueue::capacity()
        << ", batch " << batch_capacity << ", max_wait " << headline_max_wait.count() << "us) ---\n";
    write_trial(out, "offpath", offpath_result);
    out << "offpath_queue_capacity=" << ch09::HandoffQueue::capacity() << '\n';
    out << "offpath_vs_sync_mismatch_count=" << set_check.mismatch_count << '\n';
    out << "offpath_duplicate_count=" << set_check.duplicate_count << '\n';

    // --- Lane 3: backpressure demonstration, small queue, short max_wait --
    spdlog::info("backpressure lane: {} parcels, queue capacity {}, max_wait {}us...", backpressure_item_count,
                 ch09::SmallHandoffQueue::capacity(), backpressure_max_wait.count());
    const std::vector<ch09::Parcel> backpressure_parcels =
        ch09::generate_parcels(backpressure_item_count, chute_count, 0xB5B5ULL);
    const fs::path backpressure_audit_path = results_dir / ".raw" / "audit-backpressure.log";
    ch09::BlockingBatchAuditWriter backpressure_writer(backpressure_audit_path);
    const ch09::AsyncTrialResult backpressure_result = ch09::run_offpath_trial<ch09::SmallHandoffQueue>(
        backpressure_parcels, backpressure_writer, batch_capacity, max_batch_delay, backpressure_max_wait, nullptr);
    const RecordSetCheck backpressure_check = compare_record_sets(backpressure_parcels, backpressure_audit_path);

    out << "\n# --- lane 3: backpressure demo (queue capacity " << ch09::SmallHandoffQueue::capacity()
        << ", max_wait " << backpressure_max_wait.count() << "us) ---\n";
    write_trial(out, "backpressure", backpressure_result);
    out << "backpressure_queue_capacity=" << ch09::SmallHandoffQueue::capacity() << '\n';
    out << "backpressure_delivered_count="
        << (backpressure_result.completion_us.size() - backpressure_result.shed_count) << '\n';
    // Every shed parcel's record never reaches the audit file by
    // construction (push_with_backpressure returned false before the queue
    // ever saw it); missing_from_audit_count should equal shed_count
    // exactly, confirming the only parcels absent from the file are the
    // accounted-for shed ones, not a silent drop somewhere else.
    out << "backpressure_missing_from_audit_count=" << backpressure_check.mismatch_count << '\n';
    out << "backpressure_duplicate_count=" << backpressure_check.duplicate_count << '\n';

    // --- Secondary benchmark: the consumer's own batched-flush backend ----
    spdlog::info("flush benchmark: {} batches of {} records, blocking writev vs io_uring...",
                 flush_benchmark_batch_count, batch_capacity);
    std::vector<ch09::AuditRecord> bench_batch(batch_capacity);
    for (std::size_t i = 0; i < bench_batch.size(); ++i) {
        bench_batch[i].parcel_id = static_cast<std::uint64_t>(i);
        bench_batch[i].chute_id = static_cast<std::uint32_t>(i % chute_count);
    }
    const fs::path blocking_bench_path = results_dir / ".raw" / "flush-bench-blocking.log";
    ch09::BlockingBatchAuditWriter blocking_bench_writer(blocking_bench_path);
    const LatencyRecorder blocking_flush_recorder =
        benchmark_flush(blocking_bench_writer, bench_batch, flush_benchmark_batch_count);

    out << "\n# --- secondary benchmark: background consumer's own batched flush ---\n";
    out << "flush_benchmark_batch_count=" << flush_benchmark_batch_count << '\n';
    out << "flush_benchmark_batch_size=" << batch_capacity << '\n';
    write_recorder(out, "flush_blocking", blocking_flush_recorder);

#ifdef CH09_HAVE_IO_URING
    const fs::path io_uring_bench_path = results_dir / ".raw" / "flush-bench-io-uring.log";
    ch09::IoUringBatchAuditWriter io_uring_bench_writer(io_uring_bench_path);
    const LatencyRecorder io_uring_flush_recorder =
        benchmark_flush(io_uring_bench_writer, bench_batch, flush_benchmark_batch_count);
    write_recorder(out, "flush_io_uring", io_uring_flush_recorder);
#endif

    spdlog::info("sync-inline:  wall={:.3f}ms throughput={:.0f}/s", sync_result.total_wall_time_us / 1000.0,
                 static_cast<double>(headline_item_count) / (sync_result.total_wall_time_us * 1e-6));
    spdlog::info("off-path:     wall={:.3f}ms throughput={:.0f}/s shed={}", offpath_result.total_wall_time_us / 1000.0,
                 static_cast<double>(headline_item_count) / (offpath_result.total_wall_time_us * 1e-6),
                 offpath_result.shed_count);
    spdlog::info("backpressure: shed={} of {} (queue capacity {})", backpressure_result.shed_count,
                 backpressure_item_count, ch09::SmallHandoffQueue::capacity());
    spdlog::info("results written to {}", (results_dir / "async-processing.txt").string());

    return EXIT_SUCCESS;
}

}  // namespace
