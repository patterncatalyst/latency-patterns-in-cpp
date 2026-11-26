#pragma once

#include <cstddef>

namespace ch08 {

// Best-effort: pins the calling thread to one logical CPU (core_index modulo
// the host's reported concurrency) via the ordinary unprivileged
// sched_setaffinity(2) every process may already call on its own threads
// (see CLAUDE.md, "Never change the development host": this changes nothing
// about the host, only which core the calling thread's own scheduling mask
// allows it to run on). A restricted sandbox, a container without
// CAP_SYS_NICE for affinity, or a non-Linux host may refuse the call; the
// return value says whether it was honored. Every caller in this chapter
// treats false as "keep running wherever the scheduler already put this
// thread" rather than as an error, because shard-per-core's correctness
// never depends on which physical core a shard's worker actually lands on,
// only on the fact that exactly one thread owns that shard (see "The
// measurement").
bool pin_current_thread_best_effort(std::size_t core_index) noexcept;

}  // namespace ch08
