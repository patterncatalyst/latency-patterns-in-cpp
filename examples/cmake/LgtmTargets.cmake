# LgtmTargets.cmake - the shared CMake plumbing every example target (the
# line-simulator, the measurement harness, and every chapter example to
# come) uses so a warning, a sanitizer flag, or a GoogleTest registration
# never drifts between them. Kept small on purpose: this is plumbing, not a
# build system of its own.

include_guard(GLOBAL)
include(GoogleTest)

# Links the project-wide warnings/sanitizer/coverage INTERFACE libraries
# (defined in the top-level CMakeLists.txt) into an existing target.
function(lgtm_apply_common_options target)
  target_link_libraries(${target} PRIVATE
    project_warnings
    project_sanitizers
    project_coverage)
endfunction()

# Declares a GoogleTest executable, applies the common options, links
# GTest::gtest_main + GTest::gmock, and registers it with CTest via
# gtest_discover_tests. XML_OUTPUT_DIR avoids the race where parallel test
# shards clobber one results file (see lgtm-cpp skill, references/testing.md).
#
#   lgtm_add_gtest(my_tests
#     SOURCES my_test.cpp
#     LINK_LIBRARIES my_library)
function(lgtm_add_gtest target)
  cmake_parse_arguments(ARG "" "" "SOURCES;LINK_LIBRARIES" ${ARGN})
  add_executable(${target} ${ARG_SOURCES})
  target_link_libraries(${target} PRIVATE
    ${ARG_LINK_LIBRARIES}
    GTest::gtest_main
    GTest::gmock)
  lgtm_apply_common_options(${target})
  gtest_discover_tests(${target}
    XML_OUTPUT_DIR ${CMAKE_BINARY_DIR}/test-results)
endfunction()
