#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "socket.hpp"

namespace http {

// Listens on a TCP port and hands every accepted connection to a handler.
// Connections are handled sequentially for now; a thread pool comes later.
class TcpServer {
public:
    using ConnectionHandler = std::function<void(Socket&)>;

    TcpServer(std::string host, std::uint16_t port);

    // Binds and starts listening. Throws std::system_error on failure.
    void listen(int backlog = 128);

    // Runs the accept loop until stop() is called.
    void serve(const ConnectionHandler& handler);

    void stop() noexcept;

    const std::string& host() const noexcept { return host_; }
    std::uint16_t port() const noexcept { return port_; }

private:
    std::string host_;
    std::uint16_t port_;
    Socket listener_;
    std::atomic<bool> running_{false};
};

} // namespace http
