lgtm_add_gtest(ch08_shard_per_core_tests
  SOURCES routing_test.cpp shared_lock_tracker_test.cpp sharded_tracker_pool_test.cpp
          event_generator_test.cpp partition_trial_test.cpp
  LINK_LIBRARIES measurement_harness ch08_support Threads::Threads)
target_include_directories(ch08_shard_per_core_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include)
