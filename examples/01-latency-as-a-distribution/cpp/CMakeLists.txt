# Chapter 1: latency as a distribution. Drives the shared line-simulator
# under a real per-parcel scan workload plus a periodic stall, and records
# scan-to-divert naively and coordinated-omission-corrected from the same
# run (see CLAUDE.md, "The spine"; _plans/premise-probe.md for the measured
# effect).
add_library(ch01_support src/distribution_stats.cpp)
target_include_directories(ch01_support PUBLIC include)
target_link_libraries(ch01_support PUBLIC measurement_harness)
lgtm_apply_common_options(ch01_support)

add_executable(ch01_latency_as_a_distribution src/main.cpp)
target_include_directories(ch01_latency_as_a_distribution PRIVATE include)
target_link_libraries(ch01_latency_as_a_distribution PRIVATE
  line_simulator measurement_harness ch01_support)
lgtm_apply_common_options(ch01_latency_as_a_distribution)

add_subdirectory(tests)
