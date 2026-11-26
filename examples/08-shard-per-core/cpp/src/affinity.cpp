#include "ch08/affinity.hpp"

#include <thread>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace ch08 {

bool pin_current_thread_best_effort([[maybe_unused]] std::size_t core_index) noexcept {
#ifdef __linux__
    const unsigned hw_concurrency = std::thread::hardware_concurrency();
    if (hw_concurrency == 0) {
        return false;
    }
    const std::size_t core = core_index % hw_concurrency;

    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(static_cast<int>(core), &cpu_set);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set) == 0;
#else
    return false;
#endif
}

}  // namespace ch08
