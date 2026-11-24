#include "measure/scrub.hpp"

#include <spdlog/spdlog.h>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

// `environ` itself (POSIX, declared by <unistd.h>) is used read-only below,
// to hand the current process's environment through unchanged to the
// scrub-host-ids.py child; nothing here mutates it.

namespace measure {

namespace {

namespace fs = std::filesystem;

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("scrub: cannot open " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_file(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("scrub: cannot write " + path.string());
    }
    out << content;
}

// An mkstemp()-backed temp file: atomic create-and-open, so there is no
// window between checking a name is free and creating it (no symlink-race,
// no TOCTOU). Always mode 0600. Removed and closed unconditionally.
class TempFile {
public:
    explicit TempFile(const fs::path& dir) {
        std::string pattern = (dir / "scrub-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        fd_ = ::mkstemp(buffer.data());
        if (fd_ < 0) {
            throw std::runtime_error("scrub: mkstemp failed in " + dir.string());
        }
        path_ = fs::path(buffer.data());
    }

    ~TempFile() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        std::error_code ec;
        fs::remove(path_, ec);  // best effort; nothing sensitive survives a failed remove
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    int fd_ = -1;
    fs::path path_;
};

void write_all(int fd, const std::string& content) {
    std::size_t written = 0;
    while (written < content.size()) {
        const ssize_t n = ::write(fd, content.data() + written, content.size() - written);
        if (n < 0) {
            throw std::runtime_error("scrub: write to temp file failed");
        }
        written += static_cast<std::size_t>(n);
    }
    if (::lseek(fd, 0, SEEK_SET) < 0) {
        throw std::runtime_error("scrub: lseek on temp file failed");
    }
}

void scrub_file_in_place(const fs::path& file, const fs::path& scrub_script,
                          const fs::path& python3) {
    const std::string content = read_file(file);

    TempFile stdin_tmp(file.parent_path());
    TempFile stdout_tmp(file.parent_path());
    write_all(stdin_tmp.fd(), content);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdin_tmp.fd(), STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stdout_tmp.fd(), STDOUT_FILENO);

    // argv is a fixed, hardcoded shape: no shell, no string interpolation of
    // file contents or paths into a command line, so there is nothing here
    // for a crafted filename or file body to inject into. Each argument is
    // its own std::string (never a string-literal const_cast) so execve's
    // non-const char* argv has somewhere legitimate to point.
    std::vector<std::string> arg_strings{python3.string(), "-I", scrub_script.string(), "--filter"};
    std::vector<char*> argv;
    argv.reserve(arg_strings.size() + 1);
    for (std::string& arg : arg_strings) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawn_rc =
        posix_spawnp(&pid, arg_strings[0].c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawn_rc != 0) {
        throw std::runtime_error("scrub: posix_spawnp failed for " + arg_strings[0]);
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        throw std::runtime_error("scrub: waitpid failed for " + file.string());
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("scrub: scrub-host-ids.py exited non-zero for " + file.string());
    }

    write_file(file, read_file(stdout_tmp.path()));
}

}  // namespace

void scrub_results_dir(const fs::path& dir, const fs::path& scrub_script, const fs::path& python3) {
    if (!fs::exists(dir)) {
        throw std::runtime_error("scrub_results_dir: directory does not exist: " + dir.string());
    }
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            scrub_file_in_place(entry.path(), scrub_script, python3);
        }
    }
}

ResultsDirScrubGuard::ResultsDirScrubGuard(fs::path results_dir, fs::path scrub_script,
                                            fs::path python3)
    : results_dir_(std::move(results_dir)),
      scrub_script_(std::move(scrub_script)),
      python3_(std::move(python3)) {}

ResultsDirScrubGuard::~ResultsDirScrubGuard() {
    if (results_dir_.empty()) {
        return;  // moved-from
    }
    try {
        scrub_results_dir(results_dir_, scrub_script_, python3_);
    } catch (const std::exception& ex) {
        spdlog::error("ResultsDirScrubGuard: scrub failed for {}: {}", results_dir_.string(),
                      ex.what());
    } catch (...) {
        spdlog::error("ResultsDirScrubGuard: scrub failed for {} with an unknown exception",
                      results_dir_.string());
    }
}

}  // namespace measure
