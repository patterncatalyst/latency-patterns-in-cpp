#include "ch04/false_sharing.hpp"

#include <pthread.h>
#include <sched.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

namespace ch04 {

namespace {

std::optional<int> read_core_id(int cpu_id) {
    const std::string path =
        "/sys/devices/system/cpu/cpu" + std::to_string(cpu_id) + "/topology/core_id";
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }
    int core_id = -1;
    in >> core_id;
    if (!in) {
        return std::nullopt;
    }
    return core_id;
}

// Runs one two-thread trial for whichever Counters layout the caller picked,
// pinning each worker to a distinct physical core when the topology lookup
// succeeds. Kept local to this translation unit: the layout under test is a
// compile-time choice (see run_unpadded_trial / run_padded_trial), not a
// generic facility the rest of the chapter needs.
template <typename Counters>
FalseSharingTrialResult run_trial(std::uint64_t iterations_per_thread) {
    // Heap-allocated rather than a stack local: ::operator new guarantees at
    // least __STDCPP_DEFAULT_NEW_ALIGNMENT__ (16 bytes on this target)
    // alignment for any type whose own alignment does not exceed it, and 16
    // divides the 64-byte cache line evenly, so a 16-byte UnpaddedCounters
    // can never straddle a line boundary by chance. A stack local only
    // inherits alignof(Counters) (8, from the atomics), which a compiler's
    // stack-slot placement is free to start at any 8-byte offset; this
    // chapter measured that difference directly, by running this same demo
    // with a stack local first, as a false-sharing effect that appeared on
    // most runs and silently vanished on others whenever the stack offset
    // happened to split the two counters across a line boundary by luck
    // (see the chapter prose, "What actually got fixed").
    auto counters = std::make_unique<Counters>();
    const std::optional<std::pair<int, int>> cores = pin_to_distinct_cores_candidates();

    auto worker = [iterations_per_thread](std::atomic<std::uint64_t>& counter, int cpu_id) {
        if (cpu_id >= 0) {
            pin_current_thread_to_cpu(cpu_id);
        }
        for (std::uint64_t i = 0; i < iterations_per_thread; ++i) {
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    };

    const int scan_cpu = cores ? cores->first : -1;
    const int divert_cpu = cores ? cores->second : -1;

    const auto start = std::chrono::steady_clock::now();
    std::thread scan_thread(worker, std::ref(counters->scan()), scan_cpu);
    std::thread divert_thread(worker, std::ref(counters->divert()), divert_cpu);
    scan_thread.join();
    divert_thread.join();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    return FalseSharingTrialResult{
        .elapsed = elapsed,
        .scan_total = counters->scan().load(std::memory_order_relaxed),
        .divert_total = counters->divert().load(std::memory_order_relaxed),
    };
}

}  // namespace

std::optional<std::pair<int, int>> pin_to_distinct_cores_candidates() {
    const unsigned int cpu_count = std::thread::hardware_concurrency();
    if (cpu_count == 0) {
        return std::nullopt;
    }

    std::map<int, int> first_cpu_for_core;  // core_id -> first logical cpu seen
    for (unsigned int cpu = 0; cpu < cpu_count; ++cpu) {
        const std::optional<int> core_id = read_core_id(static_cast<int>(cpu));
        if (!core_id) {
            return std::nullopt;  // topology unreadable; don't guess
        }
        first_cpu_for_core.try_emplace(*core_id, static_cast<int>(cpu));
        if (first_cpu_for_core.size() >= 2) {
            auto it = first_cpu_for_core.begin();
            const int cpu_a = it->second;
            ++it;
            const int cpu_b = it->second;
            return std::make_pair(cpu_a, cpu_b);
        }
    }
    return std::nullopt;  // fewer than two distinct physical cores visible
}

bool pin_current_thread_to_cpu(int cpu_id) {
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(cpu_id, &cpu_set);
    const int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);
    if (rc != 0) {
        spdlog::warn("pin_current_thread_to_cpu({}) failed (rc={}); running unpinned", cpu_id, rc);
        return false;
    }
    return true;
}

FalseSharingTrialResult run_unpadded_trial(std::uint64_t iterations_per_thread) {
    return run_trial<UnpaddedCounters>(iterations_per_thread);
}

FalseSharingTrialResult run_padded_trial(std::uint64_t iterations_per_thread) {
    return run_trial<PaddedCounters>(iterations_per_thread);
}

}  // namespace ch04
