lgtm_add_gtest(ch07_caching_tests
  SOURCES route_resolver_test.cpp zipf_trace_test.cpp lru_cache_test.cpp
          lfu_cache_test.cpp fifo_cache_test.cpp sieve_cache_test.cpp
          ttl_lru_cache_test.cpp memoizer_test.cpp dimension_classifier_test.cpp
  LINK_LIBRARIES measurement_harness ch07_support)
target_include_directories(ch07_caching_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include)
