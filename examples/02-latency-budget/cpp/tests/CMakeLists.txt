lgtm_add_gtest(ch02_latency_budget_tests
  SOURCES budget_model_test.cpp pipeline_timing_test.cpp stage_workloads_test.cpp
  LINK_LIBRARIES line_simulator measurement_harness ch02_support)
target_include_directories(ch02_latency_budget_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include)
