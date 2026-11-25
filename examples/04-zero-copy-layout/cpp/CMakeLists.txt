# Chapter 4: zero-copy data layout. Drives the shared line-simulator twice
# over the same seeded parcel sequence for the ledger-layout lane, and runs a
# standalone two-thread false-sharing lane, both from one executable (see
# CLAUDE.md, "The spine"; _plans/premise-probe.md for both measured effects).
add_library(ch04_support
  src/parcel_ledger_before.cpp
  src/parcel_ledger_after.cpp
  src/layout_scan_stage.cpp
  src/false_sharing.cpp)
target_include_directories(ch04_support PUBLIC include)
target_link_libraries(ch04_support PUBLIC line_simulator measurement_harness Threads::Threads)
lgtm_apply_common_options(ch04_support)

add_executable(ch04_zero_copy_layout src/main.cpp)
target_include_directories(ch04_zero_copy_layout PRIVATE include)
target_link_libraries(ch04_zero_copy_layout PRIVATE
  line_simulator measurement_harness ch04_support Threads::Threads)
lgtm_apply_common_options(ch04_zero_copy_layout)

add_subdirectory(tests)
