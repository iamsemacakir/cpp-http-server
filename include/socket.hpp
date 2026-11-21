#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace http {

// Move-only RAII wrapper around a POSIX socket file descriptor.
// The descriptor is closed automatically when the object goes out of scope.
class Socket {
public:
    Socket() noexcept = default;
    explicit Socket(int fd) noexcept;
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    // Creates a new IPv4 TCP socket. Throws std::system_error on failure.
    static Socket create_tcp();

    void set_reuse_address(bool enabled);
    void bind(const std::string& host, std::uint16_t port);
    void listen(int backlog);

    // Blocks until a client connects and returns the connected socket.
    Socket accept() const;

    // Returns the local port the socket is bound to (useful with port 0).
    std::uint16_t local_port() const;

    // Address of the connected peer as "ip:port", or "unknown".
    std::string peer_address() const;

    // Sends the whole buffer, retrying on partial writes.
    void send_all(std::string_view data) const;

    // Reads up to max_bytes. Returns an empty string when the peer closed
    // the connection.
    std::string receive(std::size_t max_bytes = 4096) const;

    void close() noexcept;

    int fd() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

private:
    int fd_ = -1;
};

} // namespace http
