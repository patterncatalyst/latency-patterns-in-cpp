lgtm_add_gtest(ch01_latency_as_a_distribution_tests
  SOURCES distribution_stats_test.cpp loaded_scan_stage_test.cpp
  LINK_LIBRARIES line_simulator measurement_harness ch01_support)
target_include_directories(ch01_latency_as_a_distribution_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include)
