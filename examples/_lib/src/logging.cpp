#include "measure/logging.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>
#include <vector>

namespace measure {

void init_logging(std::string_view app_name) {
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    // 5 files, 5 MB each, under logs/ (gitignored).
    auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/" + std::string(app_name) + ".log", 5 * 1024 * 1024, 5);

    std::vector<spdlog::sink_ptr> sinks{console, file};
    auto logger =
        std::make_shared<spdlog::logger>(std::string(app_name), sinks.begin(), sinks.end());

    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::warn);  // never lose a warning/error on crash
    logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] [%n] %v");

    spdlog::set_default_logger(logger);
}

}  // namespace measure
