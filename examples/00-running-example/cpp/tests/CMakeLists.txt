lgtm_add_gtest(ch00_running_example_tests
  SOURCES recording_probe_test.cpp
  LINK_LIBRARIES line_simulator measurement_harness)
target_include_directories(ch00_running_example_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include)
