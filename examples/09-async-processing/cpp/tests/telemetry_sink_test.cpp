#include "ch09/telemetry_sink.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <thread>

namespace ch09 {
namespace {

TEST(TelemetrySinkTest, EmptyEndpointDisablesExportEntirely) {
    TelemetrySink sink("");
    EXPECT_FALSE(sink.enabled());
    sink.record_batch_flush(64, 123.4);  // must be a no-op, not a crash or a hang
    EXPECT_EQ(sink.export_attempt_count(), 0U);
}

TEST(TelemetrySinkTest, MalformedEndpointDisablesExport) {
    TelemetrySink sink("not-a-host-port-pair");
    EXPECT_FALSE(sink.enabled());
}

TEST(TelemetrySinkTest, EnabledSinkPostsToTheConfiguredEndpoint) {
    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // let the kernel pick a free port
    ASSERT_EQ(::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    socklen_t addr_len = sizeof(addr);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len), 0);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    ASSERT_EQ(::listen(listen_fd, 1), 0);
    const std::uint16_t port = ntohs(addr.sin_port);

    std::string received;
    std::thread server([&] {
        const int client_fd = ::accept(listen_fd, nullptr, nullptr);
        if (client_fd >= 0) {
            std::array<char, 4096> buffer{};
            const ssize_t count = ::recv(client_fd, buffer.data(), buffer.size(), 0);
            if (count > 0) {
                received.assign(buffer.data(), static_cast<std::size_t>(count));
            }
            ::close(client_fd);
        }
    });

    TelemetrySink sink("127.0.0.1:" + std::to_string(port));
    ASSERT_TRUE(sink.enabled());
    sink.record_batch_flush(64, 123.4);
    server.join();
    ::close(listen_fd);

    EXPECT_EQ(sink.export_attempt_count(), 1U);
    EXPECT_NE(received.find("POST /v1/metrics"), std::string::npos);
    EXPECT_NE(received.find("ch09.batch_size"), std::string::npos);
    EXPECT_NE(received.find("ch09.flush_duration_us"), std::string::npos);
}

}  // namespace
}  // namespace ch09
