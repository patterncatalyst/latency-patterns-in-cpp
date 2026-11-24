#pragma once

#include <filesystem>

namespace measure {

// Runs every regular file in `dir` (recursively) through
// scripts/scrub-host-ids.py --filter, in place. This never reimplements that
// script's replacement table (see CLAUDE.md, "Public-repo host-id scrub
// rule": the table lives in one place); it shells out to the real thing so
// the two never drift. `python3` and `scrub_script` are explicit parameters,
// not discovered, so this stays unit-testable and never guesses a host path.
//
// Throws std::runtime_error if the script cannot be run or exits non-zero
// for any file.
void scrub_results_dir(const std::filesystem::path& dir, const std::filesystem::path& scrub_script,
                        const std::filesystem::path& python3 = "python3");

// RAII guard: scrubs `results_dir` when it goes out of scope. Wrap a
// chapter's results/ directory in one of these for the run that writes to
// it, so a leaked host identifier never survives past the writer that
// produced it. A scrub failure is logged (via spdlog, see measure::init_logging)
// rather than thrown, since a destructor must not throw; `python3 -I
// scripts/validate.py` is the actual gate that fails a commit on a leak.
class ResultsDirScrubGuard {
public:
    ResultsDirScrubGuard(std::filesystem::path results_dir, std::filesystem::path scrub_script,
                          std::filesystem::path python3 = "python3");
    ~ResultsDirScrubGuard();

    ResultsDirScrubGuard(const ResultsDirScrubGuard&) = delete;
    ResultsDirScrubGuard& operator=(const ResultsDirScrubGuard&) = delete;
    ResultsDirScrubGuard(ResultsDirScrubGuard&&) = default;
    ResultsDirScrubGuard& operator=(ResultsDirScrubGuard&&) = default;

    [[nodiscard]] const std::filesystem::path& results_dir() const noexcept { return results_dir_; }

private:
    std::filesystem::path results_dir_;
    std::filesystem::path scrub_script_;
    std::filesystem::path python3_;
};

}  // namespace measure
