#include "tcp_server.hpp"

#include <cerrno>
#include <exception>
#include <memory>
#include <system_error>
#include <utility>

#include "logger.hpp"
#include "thread_pool.hpp"

namespace http {

namespace {

// Runs the handler, logging instead of propagating failures so one broken
// connection never takes down the accept loop or a pool worker.
void run_handler(const TcpServer::ConnectionHandler& handler, Socket& client) noexcept {
    try {
        handler(client);
    } catch (const std::system_error& error) {
        const int code = error.code().value();
        if (code == EAGAIN || code == EWOULDBLOCK) {
            log_warn(client.peer_address(), " timed out, closing connection");
        } else {
            log_warn(client.peer_address(), " connection error: ", error.what());
        }
    } catch (const std::exception& error) {
        log_warn(client.peer_address(), " connection error: ", error.what());
    } catch (...) {
        log_warn(client.peer_address(), " connection error: unknown exception");
    }
}

// Keeps TcpServer::active_ accurate for the lifetime of one connection.
class ActiveGuard {
public:
    explicit ActiveGuard(std::atomic<std::size_t>& counter) noexcept : counter_(&counter) {
        counter_->fetch_add(1);
    }
    ~ActiveGuard() {
        if (counter_ != nullptr) {
            counter_->fetch_sub(1);
        }
    }
    ActiveGuard(ActiveGuard&& other) noexcept : counter_(std::exchange(other.counter_, nullptr)) {}
    ActiveGuard(const ActiveGuard&) = delete;
    ActiveGuard& operator=(const ActiveGuard&) = delete;
    ActiveGuard& operator=(ActiveGuard&&) = delete;

private:
    std::atomic<std::size_t>* counter_;
};

} // namespace

TcpServer::TcpServer(std::string host, std::uint16_t port)
    : host_(std::move(host)), port_(port) {}

void TcpServer::listen(int backlog) {
    listener_ = Socket::create_tcp();
    listener_.set_reuse_address(true);
    listener_.bind(host_, port_);
    listener_.listen(backlog);
    port_ = listener_.local_port(); // resolves port 0 to the real port
    running_ = !stop_.requested(); // stop() may already have been called
}

Socket TcpServer::accept_client() {
    Socket client;
    try {
        // Wait on the listener and the stop signal together, so stop() from
        // another thread (e.g. a signal watcher) interrupts the accept loop.
        if (wait_readable(listener_, std::chrono::milliseconds{0}, &stop_) !=
            WaitResult::Ready) {
            return Socket();
        }
        if (!running_) {
            return Socket();
        }
        client = listener_.accept();
        client.set_timeout(client_timeout_);
    } catch (const std::system_error& error) {
        if (running_) {
            log_warn("accept failed: ", error.what());
        }
        return Socket();
    }
    return client;
}

void TcpServer::serve(const ConnectionHandler& handler) {
    while (running_) {
        Socket client = accept_client();
        if (!client) {
            continue;
        }
        const ActiveGuard guard(active_);
        run_handler(handler, client);
    }
    close_listener();
}

void TcpServer::serve(ConnectionHandler handler, ThreadPool& pool) {
    // Shared so every queued task can hold it without copying the handler.
    const auto shared_handler = std::make_shared<const ConnectionHandler>(std::move(handler));

    while (running_) {
        Socket client = accept_client();
        if (!client) {
            continue;
        }

        // std::function needs a copyable callable, but Socket is move-only,
        // so the connection travels to the worker behind a shared_ptr.
        auto connection = std::make_shared<Socket>(std::move(client));
        auto guard = std::make_shared<ActiveGuard>(active_);
        try {
            pool.post([shared_handler, connection, guard] {
                run_handler(*shared_handler, *connection);
            });
        } catch (const ThreadPoolStopped&) {
            log_warn("thread pool stopped, dropping connection from ",
                     connection->peer_address());
            break;
        }
    }
    close_listener();
}

void TcpServer::stop() noexcept {
    running_ = false;
    stop_.request();
}

void TcpServer::close_listener() noexcept {
    // New clients are now refused by the OS instead of piling up in the
    // backlog while in-flight connections drain.
    listener_.close();
    log_debug("listener on port ", port_, " closed");
}

} // namespace http
