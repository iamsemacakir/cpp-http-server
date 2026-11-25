#include "stop_signal.hpp"

#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>

namespace http {

StopSignal::StopSignal() {
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        throw std::system_error(errno, std::generic_category(), "socketpair");
    }
    reader_ = Socket(fds[0]);
    writer_ = Socket(fds[1]);
}

void StopSignal::request() noexcept {
    if (requested_.exchange(true)) {
        return; // already requested, the wake-up byte is in place
    }
    const char byte = 1;
    ssize_t n = -1;
    do {
        n = ::write(writer_.fd(), &byte, 1);
    } while (n < 0 && errno == EINTR);
    // The byte is never read, so wait_fd() stays readable from now on.
}

WaitResult wait_readable(const Socket& socket, std::chrono::milliseconds timeout,
                         const StopSignal* stop) {
    pollfd fds[2] = {};
    fds[0].fd = socket.fd();
    fds[0].events = POLLIN;
    nfds_t count = 1;
    if (stop != nullptr) {
        fds[1].fd = stop->wait_fd();
        fds[1].events = POLLIN;
        count = 2;
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        int wait_ms = -1;
        if (timeout.count() > 0) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            wait_ms = left.count() > 0 ? static_cast<int>(left.count()) : 0;
        }

        const int ready = ::poll(fds, count, wait_ms);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "poll");
        }
        if (ready == 0) {
            return WaitResult::Timeout;
        }
        // POLLHUP/POLLERR also count as "ready": the next read reports them.
        if (fds[0].revents != 0) {
            return WaitResult::Ready;
        }
        return WaitResult::Stopped;
    }
}

} // namespace http
