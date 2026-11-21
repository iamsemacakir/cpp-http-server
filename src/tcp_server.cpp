#include "tcp_server.hpp"

#include <exception>
#include <iostream>
#include <system_error>
#include <utility>

namespace http {

TcpServer::TcpServer(std::string host, std::uint16_t port)
    : host_(std::move(host)), port_(port) {}

void TcpServer::listen(int backlog) {
    listener_ = Socket::create_tcp();
    listener_.set_reuse_address(true);
    listener_.bind(host_, port_);
    listener_.listen(backlog);
    port_ = listener_.local_port(); // resolves port 0 to the real port
    running_ = true;
}

void TcpServer::serve(const ConnectionHandler& handler) {
    while (running_) {
        Socket client;
        try {
            client = listener_.accept();
        } catch (const std::system_error& error) {
            if (!running_) {
                break; // listener was closed by stop()
            }
            std::cerr << "accept failed: " << error.what() << '\n';
            continue;
        }

        try {
            handler(client);
        } catch (const std::exception& error) {
            std::cerr << "connection error: " << error.what() << '\n';
        }
    }
}

void TcpServer::stop() noexcept {
    running_ = false;
    listener_.close();
}

} // namespace http
