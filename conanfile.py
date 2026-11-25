# Conan 2 recipe for latency-patterns-in-cpp.
#
# Pins every dependency exactly (see versions.env and
# _plans/dependency-pins.md); never a version range. Survey the latest
# stable before bumping a pin:
#   conan search <name> -r conancenter
#
# Build with the project's own user-space Clang (see
# scripts/toolchain-env.sh), never the Fedora-packaged compiler. Each CMake
# lane in CMakePresets.json is self-sufficient: its toolchainFile points at
# its own binaryDir, so run `conan install` once per lane, into that lane's
# binaryDir, before configuring that lane. A fresh checkout only needs the
# lane it is about to use; it does not need the default lane installed
# first.
#   eval "$(scripts/toolchain-env.sh)"
#   conan install . -pr:a conan/profiles/clang23 --build=missing \
#       -of build -s build_type=Debug             # default lane
#   conan install . -pr:a conan/profiles/clang23 --build=missing \
#       -of build-asan -s build_type=Debug         # asan-ubsan lane
#   conan install . -pr:a conan/profiles/clang23 --build=missing \
#       -of build-tsan -s build_type=Debug         # tsan lane
#   conan install . -pr:a conan/profiles/clang23 --build=missing \
#       -of build-release -s build_type=Release    # release lane
#
# Then configure the matching preset, e.g. `cmake --preset asan-ubsan`.
from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps


class LatencyPatternsConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        # GoogleTest + GoogleMock, the house test framework. 1.18.0 depends
        # on Abseil; Conan pins Abseil (and RE2) transitively.
        self.requires("gtest/1.18.0")

        # Structured logging (console + rotating file) for the shared line
        # simulator and measurement harness.
        self.requires("spdlog/1.17.0")

    def generate(self):
        # No cmake_layout: with `conan install . -of <dir>`, the toolchain
        # and dep configs land flat in <dir>/, where CMakePresets.json's
        # toolchainFile (<dir>/conan_toolchain.cmake) expects them.
        tc = CMakeToolchain(self, generator="Ninja")
        # This repo installs into several output folders (build, build-asan,
        # build-tsan, build-release; see CMakePresets.json's own named
        # presets). Each one's generated CMakePresets.json otherwise
        # registers the same unscoped "conan-debug" preset name into the
        # shared root CMakeUserPresets.json, which CMake then rejects as a
        # duplicate. This repo's own CMakePresets.json already names every
        # lane explicitly, so Conan's auto-registered preset is redundant.
        tc.user_presets_path = False
        tc.generate()
        CMakeDeps(self).generate()
