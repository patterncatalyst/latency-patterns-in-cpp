#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <utility>

namespace ch04 {

// Two independent hot counters (standing in for a scan-stage counter and a
// divert-stage counter, each owned by its own thread) with nothing between
// them: on a 64-byte cache line, both atomics live in the same line, so
// every increment from one thread invalidates the other thread's cached
// copy of that line even though the two threads never touch each other's
// variable. See PaddedCounters for the fix.
struct UnpaddedCounters {
    std::atomic<std::uint64_t> scan_count{0};
    std::atomic<std::uint64_t> divert_count{0};

    [[nodiscard]] std::atomic<std::uint64_t>& scan() noexcept { return scan_count; }
    [[nodiscard]] std::atomic<std::uint64_t>& divert() noexcept { return divert_count; }
};

// The fix: each counter padded out to its own
// std::hardware_destructive_interference_size-aligned slot, so the two
// counters can never share a cache line regardless of where the compiler or
// allocator places the enclosing object.
struct PaddedCounters {
    struct alignas(std::hardware_destructive_interference_size) Slot {
        std::atomic<std::uint64_t> value{0};
    };

    Slot scan_slot;
    Slot divert_slot;

    [[nodiscard]] std::atomic<std::uint64_t>& scan() noexcept { return scan_slot.value; }
    [[nodiscard]] std::atomic<std::uint64_t>& divert() noexcept { return divert_slot.value; }
};

// These two checks are the behavioral proof that the two types actually
// differ the way the prose claims, not just an assertion in a comment: a
// build fails here before it ever gets to measuring anything, if either
// layout stops matching its name.
static_assert(offsetof(UnpaddedCounters, divert_count) - offsetof(UnpaddedCounters, scan_count) <
                  std::hardware_destructive_interference_size,
              "UnpaddedCounters's two atomics must share a cache line for this demo to mean anything");
static_assert(offsetof(PaddedCounters, divert_slot) - offsetof(PaddedCounters, scan_slot) >=
                  std::hardware_destructive_interference_size,
              "PaddedCounters's two slots must live on separate cache lines");

struct FalseSharingTrialResult {
    std::chrono::duration<double> elapsed{};
    std::uint64_t scan_total = 0;
    std::uint64_t divert_total = 0;
};

// Runs one trial: two threads, one incrementing the counters' scan() slot
// and one incrementing their divert() slot, each iterations_per_thread
// times with a relaxed fetch_add, best-effort pinned to two different
// physical cores (see pin_current_thread_to_cpu). Returns the wall-clock
// elapsed time for both threads to finish, plus each counter's final value
// (always iterations_per_thread, regardless of layout: see
// tests/false_sharing_test.cpp). Two overloads, one per layout under test,
// rather than a header-only template: the layout is a compile-time choice
// made once per lane, not a generic algorithm this chapter needs to expose.
[[nodiscard]] FalseSharingTrialResult run_unpadded_trial(std::uint64_t iterations_per_thread);
[[nodiscard]] FalseSharingTrialResult run_padded_trial(std::uint64_t iterations_per_thread);

// Reads /sys/devices/system/cpu/cpu*/topology/core_id (read-only; no
// privilege required) to find two logical CPUs that sit on two different
// physical cores. Returns std::nullopt if the topology cannot be determined
// (e.g. the sysfs tree is unavailable), in which case the caller runs
// unpinned rather than guessing a host-specific CPU number.
[[nodiscard]] std::optional<std::pair<int, int>> pin_to_distinct_cores_candidates();

// Best-effort: pins the calling thread to the given logical CPU via
// sched_setaffinity. Returns true on success. Never touches system-wide
// configuration (see CLAUDE.md, "Never change the development host"): this
// affects only the calling process's own thread.
bool pin_current_thread_to_cpu(int cpu_id);

}  // namespace ch04
