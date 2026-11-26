# examples/CMakeLists.txt - the shared pieces every chapter example builds
# against, plus one subdirectory per chapter example.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(LgtmTargets)

add_subdirectory(common/line-simulator)
add_subdirectory(_lib)

add_subdirectory(00-running-example/cpp)
add_subdirectory(01-latency-as-a-distribution/cpp)
add_subdirectory(02-latency-budget/cpp)
add_subdirectory(03-eliminating-work/cpp)
add_subdirectory(04-zero-copy-layout/cpp)
add_subdirectory(05-wait-free-sync/cpp)
add_subdirectory(06-exploiting-concurrency/cpp)
add_subdirectory(07-caching/cpp)
add_subdirectory(08-shard-per-core/cpp)
