# Top-level CMake build for latency-patterns-in-cpp.
#
# C++23, warnings-as-errors, Conan-provided dependencies, sanitizer lanes,
# clang-tidy on every compile. This file defines the shared compiler-option
# targets every example links against; the examples themselves live under
# examples/ (see examples/CMakeLists.txt). No chapter example exists yet
# (see CLAUDE.md, "Scope discipline": r00 ships the toolchain, the shared
# line-simulator, and the shared measurement harness only).
#
# Build via presets (see CMakePresets.json), which consume the Conan
# toolchain and this repo's own user-space Clang (scripts/toolchain-env.sh):
#   eval "$(scripts/toolchain-env.sh)"
#   conan install . -pr:a conan/profiles/clang23 --build=missing \
#       -of build -s build_type=Debug
#   cmake --preset default && cmake --build --preset default
#   ctest --preset default --output-on-failure
cmake_minimum_required(VERSION 3.25)
project(latency_patterns_in_cpp LANGUAGES CXX)

# ── Language standard: C++23, required, no compiler extensions ────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json for clangd and clang-tidy (editors + CI read it).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ─────────────────────────────────────────────────────────────────
option(ENABLE_CLANG_TIDY "Run clang-tidy on every compile" ON)
option(ENABLE_ASAN_UBSAN "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN       "Build with ThreadSanitizer (never combine with ASAN_UBSAN)" OFF)
option(ENABLE_COVERAGE   "Build with LLVM source-based coverage instrumentation" OFF)

if(ENABLE_ASAN_UBSAN AND ENABLE_TSAN)
  message(FATAL_ERROR "ASan/UBSan and TSan cannot be combined. Use one sanitizer lane.")
endif()

# ── Warnings-as-errors for every target ──────────────────────────────────────
add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion)

# ── Sanitizer flags, applied per lane ─────────────────────────────────────
add_library(project_sanitizers INTERFACE)
if(ENABLE_ASAN_UBSAN)
  target_compile_options(project_sanitizers INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer -g)
  target_link_options(project_sanitizers INTERFACE -fsanitize=address,undefined)
endif()
if(ENABLE_TSAN)
  target_compile_options(project_sanitizers INTERFACE
    -fsanitize=thread -fno-omit-frame-pointer -g)
  target_link_options(project_sanitizers INTERFACE -fsanitize=thread)
endif()

# ── Coverage instrumentation (LLVM source-based), applied when requested ───
add_library(project_coverage INTERFACE)
if(ENABLE_COVERAGE)
  target_compile_options(project_coverage INTERFACE
    -fprofile-instr-generate -fcoverage-mapping -g -O0)
  target_link_options(project_coverage INTERFACE -fprofile-instr-generate)
endif()

# ── clang-tidy on every compile (gated so a fast build can skip it) ────────
# Prefer this repo's own pinned clang-tidy over whatever the host has on
# PATH (see CLAUDE.md, "Dependencies": no Fedora-packaged toolchain on a
# measured path).
if(ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy
    HINTS "${CMAKE_SOURCE_DIR}/.toolchain/llvm-23.1.6/bin")
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  else()
    message(WARNING "clang-tidy not found; analysis disabled for this build")
  endif()
endif()

# ── Dependencies, resolved from Conan's CMakeDeps ────────────────────────
find_package(spdlog REQUIRED)
find_package(GTest REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
# For the harness's scrub test only: it shells out to the real
# scripts/scrub-host-ids.py, so the test needs a real interpreter.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

enable_testing()
add_subdirectory(examples)
