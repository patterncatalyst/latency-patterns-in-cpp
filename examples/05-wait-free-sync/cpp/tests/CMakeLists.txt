lgtm_add_gtest(ch05_wait_free_sync_tests
  SOURCES mutex_handoff_queue_test.cpp spsc_ring_queue_test.cpp handoff_trial_test.cpp
  LINK_LIBRARIES measurement_harness ch05_support Threads::Threads)
target_include_directories(ch05_wait_free_sync_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include)
