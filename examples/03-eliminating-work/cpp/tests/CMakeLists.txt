lgtm_add_gtest(ch03_eliminating_work_tests
  SOURCES divert_manifest_test.cpp before_after_divert_test.cpp
  LINK_LIBRARIES line_simulator measurement_harness ch03_support)
target_include_directories(ch03_eliminating_work_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include)
