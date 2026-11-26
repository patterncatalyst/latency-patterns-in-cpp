#pragma once

#include <unistd.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace ch09 {
namespace testutil {

// A fresh, unique temp directory per call (via mkdtemp), the same pattern
// examples/_lib/tests/scrub_test.cpp uses. A fixed temp filename would
// collide across concurrently-running ctest jobs: CTest's default test
// preset runs with jobs=0 (as many in parallel as the host has cores), so
// two GoogleTest processes for two different TEST_F cases in the same
// fixture can be mid-write to the identical path at the same instant.
inline std::filesystem::path make_unique_test_dir(const std::string& prefix) {
    std::string pattern = (std::filesystem::temp_directory_path() / (prefix + "-XXXXXX")).string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char* result = ::mkdtemp(buffer.data());
    if (result == nullptr) {
        throw std::runtime_error("make_unique_test_dir: mkdtemp failed for prefix " + prefix);
    }
    return {result};
}

}  // namespace testutil
}  // namespace ch09
