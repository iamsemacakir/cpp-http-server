#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "tcp_server.hpp"
#include "version.hpp"

namespace {

std::uint16_t parse_port(int argc, char** argv) {
    if (argc < 2) {
        return 8080;
    }
    const int port = std::stoi(argv[1]);
    if (port < 0 || port > 65535) {
        throw std::out_of_range("port must be between 0 and 65535");
    }
    return static_cast<std::uint16_t>(port);
}

void handle_connection(http::Socket& client) {
    const std::string request = client.receive();
    if (request.empty()) {
        return;
    }

    const std::string body = "Hello from cpp-http-server!\n";
    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;
    client.send_all(response);
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);

    try {
        http::TcpServer server("0.0.0.0", parse_port(argc, argv));
        server.listen();
        std::cout << http::kServerName << " v" << http::kServerVersion
                  << " listening on http://localhost:" << server.port() << '\n';
        server.serve(handle_connection);
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
