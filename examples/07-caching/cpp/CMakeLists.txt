# Chapter 7: caching. One Zipf-skewed barcode-to-route access trace, run
# uncached and then through four replacement-policy caches in front of the
# same slow RouteResolver, plus a TTL trial across a simulated reroute and a
# memoized pure-function trial (see CLAUDE.md, "The spine";
# _plans/premise-probe.md for the measured per-policy effect).
add_library(ch07_support
  src/route_resolver.cpp
  src/zipf_trace.cpp
  src/dimension_classifier.cpp
  src/cache_trial.cpp)
target_include_directories(ch07_support PUBLIC include)
target_link_libraries(ch07_support PUBLIC measurement_harness)
lgtm_apply_common_options(ch07_support)

add_executable(ch07_caching src/main.cpp)
target_include_directories(ch07_caching PRIVATE include)
target_link_libraries(ch07_caching PRIVATE measurement_harness ch07_support)
lgtm_apply_common_options(ch07_caching)

add_subdirectory(tests)
