# Chapter 0: the running example and the measurement harness. Drives the
# shared line-simulator once at its house defaults and records the baseline
# scan-to-divert distribution; no technique is applied yet (see CLAUDE.md,
# "The spine"). Later chapters' examples compare their lane against this run.
add_executable(ch00_running_example src/main.cpp)
target_include_directories(ch00_running_example PRIVATE include)
target_link_libraries(ch00_running_example PRIVATE line_simulator measurement_harness)
lgtm_apply_common_options(ch00_running_example)

add_subdirectory(tests)
