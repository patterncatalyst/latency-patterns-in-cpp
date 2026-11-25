lgtm_add_gtest(ch04_zero_copy_layout_tests
  SOURCES layout_scan_stage_test.cpp false_sharing_test.cpp
  LINK_LIBRARIES line_simulator measurement_harness ch04_support Threads::Threads)
target_include_directories(ch04_zero_copy_layout_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include)
