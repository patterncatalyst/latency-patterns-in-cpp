# examples/CMakeLists.txt - the shared pieces every chapter example will
# build against. No chapter example lives here yet (see CLAUDE.md, "Scope
# discipline"): this iteration ships the shared line-simulator and the
# shared measurement harness, plus their tests, and nothing else.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(LgtmTargets)

add_subdirectory(common/line-simulator)
add_subdirectory(_lib)
