#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "http_request.hpp"
#include "http_response.hpp"
#include "request_reader.hpp"
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

void send(http::Socket& client, http::HttpResponse response, bool head_only = false) {
    response.set_header("Connection", "close");
    client.send_all(response.serialize(head_only));
}

void handle_connection(http::Socket& client) {
    try {
        const auto request = http::read_request(client);
        if (!request) {
            return;
        }

        std::string body = "Hello from cpp-http-server!\n";
        body += "method:  " + std::string(http::to_string(request->method)) + "\n";
        body += "path:    " + request->path + "\n";
        if (!request->query.empty()) {
            body += "query:   " + request->query + "\n";
        }
        body += "headers: " + std::to_string(request->headers.size()) + "\n";
        if (const auto agent = request->header("User-Agent")) {
            body += "agent:   " + *agent + "\n";
        }
        if (!request->body.empty()) {
            body += "body:    " + request->body + "\n";
        }
        std::cout << to_string(request->method) << ' ' << request->target << '\n';
        send(client, http::HttpResponse::text(http::status::kOk, body),
             request->method == http::Method::Head);
    } catch (const http::RequestTooLarge& error) {
        send(client, http::HttpResponse::text(http::status::kPayloadTooLarge, std::string(error.what()) + "\n"));
    } catch (const http::ParseError& error) {
        send(client, http::HttpResponse::text(http::status::kBadRequest, std::string(error.what()) + "\n"));
    }
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
