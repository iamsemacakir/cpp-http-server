#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "socket.hpp"

namespace http {

class ThreadPool;

// Listens on a TCP port and hands every accepted connection to a handler,
// either on the accepting thread or on the workers of a ThreadPool.
class TcpServer {
public:
    using ConnectionHandler = std::function<void(Socket&)>;

    // Idle time after which a client that stops sending/reading is dropped.
    static constexpr std::chrono::milliseconds kDefaultClientTimeout{10'000};

    TcpServer(std::string host, std::uint16_t port);

    // Binds and starts listening. Throws std::system_error on failure.
    void listen(int backlog = 128);

    // Runs the accept loop until stop() is called, handling each connection
    // on the calling thread (one client at a time).
    void serve(const ConnectionHandler& handler);

    // Runs the accept loop until stop() is called and hands every accepted
    // connection to `pool`, so slow clients don't block each other. The
    // handler is called concurrently and must be thread-safe; it and the
    // pool must outlive any connection still in flight.
    void serve(ConnectionHandler handler, ThreadPool& pool);

    void stop() noexcept;

    void set_client_timeout(std::chrono::milliseconds timeout) noexcept {
        client_timeout_ = timeout;
    }
    std::chrono::milliseconds client_timeout() const noexcept { return client_timeout_; }

    // Connections currently being handled (queued or running on the pool).
    std::size_t active_connections() const noexcept { return active_.load(); }

    const std::string& host() const noexcept { return host_; }
    std::uint16_t port() const noexcept { return port_; }

private:
    // Blocks for the next client; returns an invalid socket when the
    // listener was stopped or accept() failed (the failure is logged).
    Socket accept_client();

    std::string host_;
    std::uint16_t port_;
    Socket listener_;
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> active_{0};
    std::chrono::milliseconds client_timeout_{kDefaultClientTimeout};
};

} // namespace http
