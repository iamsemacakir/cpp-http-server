#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "socket.hpp"
#include "stop_signal.hpp"

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

    // Makes serve() stop accepting, close the listening socket and return.
    // Connections already accepted are left to finish; handlers can watch
    // stop_signal() to wind down idle keep-alive connections. Safe to call
    // from any thread and more than once.
    void stop() noexcept;
    bool stopping() const noexcept { return stop_.requested(); }

    // Requested by stop(); pass it to serve_connection() for graceful shutdown.
    const StopSignal& stop_signal() const noexcept { return stop_; }

    void set_client_timeout(std::chrono::milliseconds timeout) noexcept {
        client_timeout_ = timeout;
    }
    std::chrono::milliseconds client_timeout() const noexcept { return client_timeout_; }

    // Connections currently being handled (queued or running on the pool).
    std::size_t active_connections() const noexcept { return active_.load(); }

    const std::string& host() const noexcept { return host_; }
    std::uint16_t port() const noexcept { return port_; }

private:
    // Blocks for the next client; returns an invalid socket when stop() was
    // called or accept() failed (the failure is logged).
    Socket accept_client();
    void close_listener() noexcept;

    std::string host_;
    std::uint16_t port_;
    Socket listener_;
    StopSignal stop_;
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> active_{0};
    std::chrono::milliseconds client_timeout_{kDefaultClientTimeout};
};

} // namespace http
