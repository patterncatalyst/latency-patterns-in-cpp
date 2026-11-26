lgtm_add_gtest(ch09_async_processing_tests
  SOURCES parcel_test.cpp buffer_pool_test.cpp bounded_handoff_queue_test.cpp
          sync_audit_writer_test.cpp batch_audit_writer_test.cpp
          io_uring_batch_writer_test.cpp event_loop_consumer_test.cpp
          hot_path_test.cpp async_trial_test.cpp telemetry_sink_test.cpp
  LINK_LIBRARIES measurement_harness ch09_support Threads::Threads)
target_include_directories(ch09_async_processing_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include)
