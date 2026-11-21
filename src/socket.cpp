#include "socket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace http {

namespace {

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

} // namespace

Socket::Socket(int fd) noexcept : fd_(fd) {}

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

Socket Socket::create_tcp() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw_errno("socket");
    }
    return Socket(fd);
}

void Socket::set_reuse_address(bool enabled) {
    const int value = enabled ? 1 : 0;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) < 0) {
        throw_errno("setsockopt(SO_REUSEADDR)");
    }
}

void Socket::bind(const std::string& host, std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        throw std::system_error(EINVAL, std::generic_category(),
                                "invalid IPv4 address: " + host);
    }
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        throw_errno("bind");
    }
}

void Socket::listen(int backlog) {
    if (::listen(fd_, backlog) < 0) {
        throw_errno("listen");
    }
}

Socket Socket::accept() const {
    int client = -1;
    do {
        client = ::accept(fd_, nullptr, nullptr);
    } while (client < 0 && errno == EINTR);

    if (client < 0) {
        throw_errno("accept");
    }
    return Socket(client);
}

std::uint16_t Socket::local_port() const {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
        throw_errno("getsockname");
    }
    return ntohs(address.sin_port);
}

void Socket::send_all(std::string_view data) const {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, kSendFlags);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw_errno("send");
        }
        sent += static_cast<std::size_t>(n);
    }
}

std::string Socket::receive(std::size_t max_bytes) const {
    std::string buffer(max_bytes, '\0');
    ssize_t n = -1;
    do {
        n = ::recv(fd_, buffer.data(), buffer.size(), 0);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        throw_errno("recv");
    }
    buffer.resize(static_cast<std::size_t>(n));
    return buffer;
}

void Socket::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace http
