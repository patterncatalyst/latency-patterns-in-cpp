#include "ch09/telemetry_sink.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

namespace ch09 {

namespace {

constexpr std::chrono::milliseconds socket_timeout{200};

bool parse_endpoint(const std::string& endpoint, std::string& host, std::uint16_t& port) {
    const auto colon = endpoint.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon == endpoint.size() - 1) {
        return false;
    }
    host = endpoint.substr(0, colon);
    const std::string port_str = endpoint.substr(colon + 1);
    unsigned long parsed = 0;
    const auto [ptr, ec] = std::from_chars(port_str.data(), port_str.data() + port_str.size(), parsed);
    if (ec != std::errc() || ptr != port_str.data() + port_str.size() || parsed > 65535) {
        return false;
    }
    port = static_cast<std::uint16_t>(parsed);
    return true;
}

// A minimal, hand-rolled OTLP/HTTP JSON body for two gauge metrics
// (ch09.batch_size, ch09.flush_duration_us). This is not the
// opentelemetry-cpp SDK: that SDK is a large dependency to pull in for a
// demo whose point is "telemetry goes off the hot path," not "which OTel
// SDK a C++ service should use," and this chapter's background consumer
// already runs on its own thread, off the hot path, so a hand-rolled
// best-effort HTTP POST teaches the placement lesson without the weight.
// A production service would reach for opentelemetry-cpp; see "Trade-offs."
std::string build_otlp_json(std::size_t batch_size, double flush_duration_us) {
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    std::ostringstream body;
    body << R"({"resourceMetrics":[{"resource":{"attributes":[{"key":"service.name",)"
         << R"("value":{"stringValue":"ch09-async-processing"}}]},"scopeMetrics":[{"scope":)"
         << R"({"name":"ch09"},"metrics":[)"
         << R"({"name":"ch09.batch_size","unit":"1","gauge":{"dataPoints":[)"
         << R"({"timeUnixNano":")" << now_ns << R"(","asInt":")" << batch_size << R"("}]}},)"
         << R"({"name":"ch09.flush_duration_us","unit":"us","gauge":{"dataPoints":[)"
         << R"({"timeUnixNano":")" << now_ns << R"(","asDouble":)" << flush_duration_us << R"(}]}})"
         << R"(]}]}]})";
    return body.str();
}

}  // namespace

TelemetrySink::TelemetrySink(const std::string& otlp_http_endpoint) {
    if (otlp_http_endpoint.empty()) {
        return;
    }
    enabled_ = parse_endpoint(otlp_http_endpoint, host_, port_);
}

bool TelemetrySink::post_otlp_metrics(std::size_t batch_size, double flush_duration_us) noexcept {
    const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }
    const timeval timeout{.tv_sec = 0, .tv_usec = static_cast<suseconds_t>(socket_timeout.count() * 1000)};
    ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        ::close(sock);
        return false;
    }
    if (::connect(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) !=  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        0) {
        ::close(sock);
        return false;
    }

    const std::string body = build_otlp_json(batch_size, flush_duration_us);
    std::ostringstream request;
    request << "POST /v1/metrics HTTP/1.1\r\n"
            << "Host: " << host_ << ":" << port_ << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << body;
    const std::string request_str = request.str();

    std::size_t sent = 0;
    while (sent < request_str.size()) {
        const ssize_t result = ::send(sock, request_str.data() + sent, request_str.size() - sent, 0);
        if (result <= 0) {
            ::close(sock);
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    std::array<char, 256> discard{};
    while (::recv(sock, discard.data(), discard.size(), 0) > 0) {
        // Drain and discard the response; this sink is fire-and-forget.
    }
    ::close(sock);
    return true;
}

void TelemetrySink::record_batch_flush(std::size_t batch_size, double flush_duration_us) noexcept {
    if (!enabled_) {
        return;
    }
    ++export_attempt_count_;
    if (!post_otlp_metrics(batch_size, flush_duration_us)) {
        ++export_failure_count_;
    }
}

}  // namespace ch09
