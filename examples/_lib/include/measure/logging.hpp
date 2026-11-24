#pragma once

#include <string_view>

namespace measure {

// Wires spdlog to log to the console and to a rotating file under logs/
// (gitignored) so a crash or a failing test leaves a grep-able trail (see
// lgtm-cpp skill, references/logging.md). Not an observability stack: no
// Grafana/Loki/Tempo/Mimir, no OTLP export, no container. Call once, from a
// chapter example's main() or a test fixture's SetUpTestSuite().
void init_logging(std::string_view app_name);

}  // namespace measure
