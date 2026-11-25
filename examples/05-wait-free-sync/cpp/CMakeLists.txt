# Chapter 5: wait-free synchronization. One scan-to-divert handoff, measured
# through a mutex-guarded queue and a wait-free SPSC ring buffer (see
# CLAUDE.md, "The spine"; _plans/premise-probe.md for the measured effect).
add_library(ch05_support
  src/mutex_handoff_queue.cpp
  src/handoff_trial.cpp)
target_include_directories(ch05_support PUBLIC include)
target_link_libraries(ch05_support PUBLIC measurement_harness Threads::Threads)
lgtm_apply_common_options(ch05_support)

add_executable(ch05_wait_free_sync src/main.cpp)
target_include_directories(ch05_wait_free_sync PRIVATE include)
target_link_libraries(ch05_wait_free_sync PRIVATE
  measurement_harness ch05_support Threads::Threads)
lgtm_apply_common_options(ch05_wait_free_sync)

add_subdirectory(tests)
