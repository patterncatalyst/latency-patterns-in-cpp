lgtm_add_gtest(measurement_harness_tests
  SOURCES latency_recorder_test.cpp scrub_test.cpp
  LINK_LIBRARIES measurement_harness)

# scrub_test.cpp shells out to the real scripts/scrub-host-ids.py (see
# CLAUDE.md, "Public-repo host-id scrub rule": the replacement table lives
# in one place, never duplicated). These paths are compiled into the test
# binary only, which is gitignored build output, not a tracked file.
target_compile_definitions(measurement_harness_tests PRIVATE
  TEST_SCRUB_SCRIPT="${CMAKE_SOURCE_DIR}/scripts/scrub-host-ids.py"
  TEST_PYTHON3="${Python3_EXECUTABLE}")
