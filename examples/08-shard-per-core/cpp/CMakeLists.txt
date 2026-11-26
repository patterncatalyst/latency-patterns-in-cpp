# Chapter 8: shard-per-core partitioning. One in-flight parcel tracker,
# measured as a single map behind one shared lock versus shard-per-core
# (direct hash routing, no lock on the hot path), then the sharded design
# again under induced hot-key skew before and after a routing-table
# rebalance (see CLAUDE.md, "The spine"; _plans/premise-probe.md for the
# measured effect).
add_library(ch08_support
  src/shared_lock_tracker.cpp
  src/sharded_tracker_pool.cpp
  src/routing.cpp
  src/affinity.cpp
  src/event_generator.cpp
  src/partition_trial.cpp)
target_include_directories(ch08_support PUBLIC include)
target_link_libraries(ch08_support PUBLIC measurement_harness Threads::Threads)
lgtm_apply_common_options(ch08_support)

add_executable(ch08_shard_per_core src/main.cpp)
target_include_directories(ch08_shard_per_core PRIVATE include)
target_link_libraries(ch08_shard_per_core PRIVATE
  measurement_harness ch08_support Threads::Threads)
lgtm_apply_common_options(ch08_shard_per_core)

add_subdirectory(tests)
