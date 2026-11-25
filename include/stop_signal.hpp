#pragma once

#include <atomic>
#include <chrono>

#include "socket.hpp"

namespace http {

// One-shot, thread-safe "please stop" flag that blocked threads can wait on.
// request() sets the flag and makes wait_fd() readable for good, so every
// thread polling on it (the accept loop, idle keep-alive connections) wakes
// up at once. request() only stores an atomic and writes one byte, so it is
// also safe to call from a POSIX signal handler.
class StopSignal {
public:
    // Creates the internal socket pair. Throws std::system_error on failure.
    StopSignal();

    StopSignal(const StopSignal&) = delete;
    StopSignal& operator=(const StopSignal&) = delete;

    void request() noexcept;
    bool requested() const noexcept { return requested_.load(); }

    // Becomes (and stays) readable once request() was called.
    int wait_fd() const noexcept { return reader_.fd(); }

private:
    Socket reader_;
    Socket writer_;
    std::atomic<bool> requested_{false};
};

enum class WaitResult { Ready, Timeout, Stopped };

// Waits until `socket` has data (or a pending connection) to read, `timeout`
// expires, or `stop` is requested. A zero timeout waits indefinitely and a
// null `stop` only waits for the socket. When data is already waiting, Ready
// is reported even if a stop was requested.
WaitResult wait_readable(const Socket& socket, std::chrono::milliseconds timeout,
                         const StopSignal* stop);

} // namespace http
