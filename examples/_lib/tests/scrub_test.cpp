#include "measure/scrub.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#ifndef TEST_SCRUB_SCRIPT
#error "TEST_SCRUB_SCRIPT must be defined by CMake (see examples/_lib/tests/CMakeLists.txt)"
#endif
#ifndef TEST_PYTHON3
#error "TEST_PYTHON3 must be defined by CMake (see examples/_lib/tests/CMakeLists.txt)"
#endif

namespace measure {
namespace {

namespace fs = std::filesystem;

fs::path make_unique_dir() {
    std::string pattern = (fs::temp_directory_path() / "scrub-test-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char* result = ::mkdtemp(buffer.data());
    if (result == nullptr) {
        throw std::runtime_error("scrub_test: mkdtemp failed");
    }
    return {result};
}

class ScrubTest : public ::testing::Test {
protected:
    void SetUp() override { dir_ = make_unique_dir(); }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path dir_;
};

TEST_F(ScrubTest, ScrubsAnEmbeddedHomePathOutOfAFile) {
    const fs::path file = dir_ / "result.txt";
    const char* home = ::getenv("HOME");
    const std::string home_path = (home != nullptr) ? home : "/home/nobody";
    {
        std::ofstream out(file);
        out << "ran from " << home_path << "/project\n";
    }

    scrub_results_dir(dir_, TEST_SCRUB_SCRIPT, TEST_PYTHON3);

    std::ifstream in(file);
    const std::string content((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    EXPECT_EQ(content.find(home_path), std::string::npos);
    EXPECT_NE(content.find('~'), std::string::npos);
}

TEST_F(ScrubTest, LeavesOrdinaryTextUntouched) {
    const fs::path file = dir_ / "result.txt";
    {
        std::ofstream out(file);
        out << "p50=1.2ms p99=4.5ms p99.9=9.1ms\n";
    }

    scrub_results_dir(dir_, TEST_SCRUB_SCRIPT, TEST_PYTHON3);

    std::ifstream in(file);
    const std::string content((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "p50=1.2ms p99=4.5ms p99.9=9.1ms\n");
}

}  // namespace
}  // namespace measure
