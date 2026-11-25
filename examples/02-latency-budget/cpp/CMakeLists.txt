# Chapter 2: the latency budget. Drives the shared line-simulator with a
# real, timed workload at every one of the five stages and records a
# per-stage distribution plus the scan-to-divert sum (see CLAUDE.md, "The
# spine"; _plans/premise-probe.md for the measured per-stage decomposition).
add_library(ch02_support
  src/pipeline_timing.cpp
  src/stage_workloads.cpp
  src/budget_model.cpp)
target_include_directories(ch02_support PUBLIC include)
target_link_libraries(ch02_support PUBLIC line_simulator measurement_harness)
lgtm_apply_common_options(ch02_support)

add_executable(ch02_latency_budget src/main.cpp)
target_include_directories(ch02_latency_budget PRIVATE include)
target_link_libraries(ch02_latency_budget PRIVATE
  line_simulator measurement_harness ch02_support)
lgtm_apply_common_options(ch02_latency_budget)

add_subdirectory(tests)
