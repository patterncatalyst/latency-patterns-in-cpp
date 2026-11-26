# Chapter 9: asynchronous processing. The scan-to-divert hot path's audit/
# label write, measured synchronous-inline (the "before") versus handed off
# to a background event-loop consumer (the "after"): epoll multiplexing a
# queue-ready eventfd against a batch-deadline timerfd, request batching, a
# reusable buffer pool, and bounded backpressure with accounted shedding
# (see CLAUDE.md, "The spine"; _plans/premise-probe.md for the measured
# effect). liburing, if Conan resolved it, gives the batched flush a faster
# submission path than a blocking writev(); if it did not resolve (non-Linux,
# or the package build failed), the example still builds and runs on the
# epoll + blocking-writev baseline alone.
find_package(liburing QUIET)
if(TARGET liburing::liburing)
  set(CH09_HAVE_IO_URING ON)
else()
  set(CH09_HAVE_IO_URING OFF)
  message(STATUS "ch09: liburing not found; building the epoll + blocking-writev baseline only")
endif()

add_library(ch09_support
  src/parcel.cpp
  src/sync_audit_writer.cpp
  src/batch_audit_writer.cpp
  src/telemetry_sink.cpp
  src/hot_path.cpp
  src/async_trial.cpp)
target_include_directories(ch09_support PUBLIC include)
target_link_libraries(ch09_support PUBLIC measurement_harness Threads::Threads)
lgtm_apply_common_options(ch09_support)

if(CH09_HAVE_IO_URING)
  target_sources(ch09_support PRIVATE src/io_uring_batch_writer.cpp)
  target_link_libraries(ch09_support PUBLIC liburing::liburing)
  target_compile_definitions(ch09_support PUBLIC CH09_HAVE_IO_URING)
endif()

add_executable(ch09_async_processing src/main.cpp)
target_include_directories(ch09_async_processing PRIVATE include)
target_link_libraries(ch09_async_processing PRIVATE
  measurement_harness ch09_support Threads::Threads)
lgtm_apply_common_options(ch09_async_processing)

add_subdirectory(tests)
