# Chapter 3: eliminating work. Drives the shared line-simulator twice over
# the same seeded parcel sequence, once with the unoptimized scan decode
# path and once with the precomputed one, and records both distributions
# plus a behavioral chute-agreement check (see CLAUDE.md, "The spine";
# _plans/premise-probe.md for the measured allocation/lookup elimination
# effect).
add_library(ch03_support
  src/divert_manifest.cpp
  src/before_scan_stage.cpp
  src/after_scan_stage.cpp)
target_include_directories(ch03_support PUBLIC include)
target_link_libraries(ch03_support PUBLIC line_simulator measurement_harness)
lgtm_apply_common_options(ch03_support)

add_executable(ch03_eliminating_work src/main.cpp)
target_include_directories(ch03_eliminating_work PRIVATE include)
target_link_libraries(ch03_eliminating_work PRIVATE
  line_simulator measurement_harness ch03_support)
lgtm_apply_common_options(ch03_eliminating_work)

add_subdirectory(tests)
