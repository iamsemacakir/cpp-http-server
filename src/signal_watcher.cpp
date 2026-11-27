#include "signal_watcher.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>

#include "logger.hpp"

namespace http {

namespace {

void ignore_delivery(int /*signal*/) {}

}  // namespace

SignalWatcher::SignalWatcher(std::initializer_list<int> signals, Callback on_signal)
    : signals_(signals), on_signal_(std::move(on_signal)) {
    if (signals_.empty()) {
        throw std::invalid_argument("SignalWatcher needs at least one signal");
    }
    sigemptyset(&watched_);
    for (const int signal : signals_) {
        sigaddset(&watched_, signal);
    }
    // A process can inherit SIG_IGN for a signal (e.g. SIGINT for background
    // jobs of a non-interactive shell). Some systems, macOS among them,
    // discard ignored signals even while they are blocked, so sigwait() would
    // never see them. Installing a handler makes them pending as usual; it
    // never runs because the signals stay blocked.
    previous_actions_.resize(signals_.size());
    struct sigaction action {};
    action.sa_handler = ignore_delivery;
    sigemptyset(&action.sa_mask);
    for (std::size_t i = 0; i < signals_.size(); ++i) {
        if (::sigaction(signals_[i], &action, &previous_actions_[i]) != 0) {
            const int error = errno;
            for (std::size_t j = 0; j < i; ++j) {
                ::sigaction(signals_[j], &previous_actions_[j], nullptr);
            }
            throw std::system_error(error, std::generic_category(), "sigaction");
        }
    }
    if (const int error = pthread_sigmask(SIG_BLOCK, &watched_, &previous_mask_); error != 0) {
        restore_actions();
        throw std::system_error(error, std::generic_category(), "pthread_sigmask");
    }
    try {
        thread_ = std::thread(&SignalWatcher::run, this);
    } catch (...) {
        pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
        restore_actions();
        throw;
    }
}

SignalWatcher::~SignalWatcher() {
    done_ = true;
    // Wake sigwait() with one of the watched signals; run() sees done_ and
    // returns without treating it as a real signal.
    pthread_kill(thread_.native_handle(), signals_.front());
    thread_.join();
    pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
    restore_actions();
}

void SignalWatcher::restore_actions() noexcept {
    for (std::size_t i = 0; i < previous_actions_.size(); ++i) {
        ::sigaction(signals_[i], &previous_actions_[i], nullptr);
    }
}

void SignalWatcher::run() {
    for (;;) {
        int signal = 0;
        if (const int error = sigwait(&watched_, &signal); error != 0) {
            if (error == EINTR) {
                continue;
            }
            log_error("sigwait failed: ", std::strerror(error));
            return;
        }
        if (done_) {
            return;
        }

        if (received_.fetch_add(1) > 0) {
            // Second signal: the user doesn't want to wait for the drain.
            const std::string message =
                std::string("\nreceived ") + signal_name(signal) + " again, exiting now\n";
            [[maybe_unused]] const auto written =
                ::write(STDERR_FILENO, message.data(), message.size());
            std::_Exit(128 + signal);
        }

        try {
            on_signal_(signal);
        } catch (const std::exception& error) {
            log_error("signal handler failed: ", error.what());
        } catch (...) {
            log_error("signal handler failed: unknown exception");
        }
    }
}

const char* signal_name(int signal) noexcept {
    switch (signal) {
    case SIGINT:
        return "SIGINT";
    case SIGTERM:
        return "SIGTERM";
    case SIGHUP:
        return "SIGHUP";
    case SIGQUIT:
        return "SIGQUIT";
    default:
        return "signal";
    }
}

} // namespace http
