# The shared measurement harness every chapter's example links against:
# a latency distribution recorder (p50/p99/p99.9, histogram, eCDF,
# coordinated-omission correction), the results-directory host-id scrub, and
# the spdlog console/rotating-file logging setup. Kept under the harness
# line cap in scripts/validate.py.
add_library(measurement_harness
  src/latency_recorder.cpp
  src/scrub.cpp
  src/logging.cpp)
target_include_directories(measurement_harness PUBLIC include)
target_link_libraries(measurement_harness PUBLIC spdlog::spdlog)
lgtm_apply_common_options(measurement_harness)

add_subdirectory(tests)
