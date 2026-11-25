#pragma once

#include <atomic>
#include <csignal>
#include <functional>
#include <initializer_list>
#include <thread>
#include <vector>

namespace http {

// Handles POSIX signals such as SIGINT and SIGTERM on a dedicated thread
// instead of inside an async signal handler, so the callback may log, take
// locks and call into the server like any other code.
//
// The constructor blocks the signals in the calling thread, and every thread
// created afterwards inherits that mask, so construct the watcher in main()
// before starting any other thread (thread pool, etc.).
//
// The first signal invokes `on_signal`; a second one terminates the process
// immediately with exit status 128 + signal number, so an impatient user can
// always get out of a shutdown that takes too long.
class SignalWatcher {
public:
    using Callback = std::function<void(int signal)>;

    SignalWatcher(std::initializer_list<int> signals, Callback on_signal);
    ~SignalWatcher(); // stops the watcher thread and restores the signal mask

    SignalWatcher(const SignalWatcher&) = delete;
    SignalWatcher& operator=(const SignalWatcher&) = delete;

    // Number of watched signals received so far.
    int received() const noexcept { return received_.load(); }

private:
    void run();

    std::vector<int> signals_;
    Callback on_signal_;
    sigset_t watched_{};
    sigset_t previous_mask_{};
    std::atomic<bool> done_{false};
    std::atomic<int> received_{0};
    std::thread thread_;
};

// "SIGINT", "SIGTERM", "SIGHUP", "SIGQUIT", or "signal" for anything else.
const char* signal_name(int signal) noexcept;

} // namespace http
