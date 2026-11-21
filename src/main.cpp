#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "http_request.hpp"
#include "http_response.hpp"
#include "request_reader.hpp"
#include "router.hpp"
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

http::Router build_router() {
    using http::HttpRequest;
    using http::HttpResponse;

    http::Router router;

    router.get("/", [](const HttpRequest&) {
        return HttpResponse::html(http::status::kOk,
                                  "<!DOCTYPE html>\n<html><head><title>cpp-http-server</title></head>"
                                  "<body><h1>It works!</h1>"
                                  "<p>Try <a href=\"/hello\">/hello</a> or "
                                  "<a href=\"/request\">/request</a>.</p></body></html>\n");
    });

    router.get("/hello", [](const HttpRequest&) {
        return HttpResponse::text(http::status::kOk, "Hello from cpp-http-server!\n");
    });

    router.get("/health", [](const HttpRequest&) {
        HttpResponse response(http::status::kOk);
        response.set_body("{\"status\":\"ok\"}\n", "application/json");
        return response;
    });

    router.get("/request", [](const HttpRequest& request) {
        std::string body;
        body += "method:  " + std::string(http::to_string(request.method)) + "\n";
        body += "path:    " + request.path + "\n";
        body += "query:   " + request.query + "\n";
        body += "version: " + request.version + "\n";
        for (const auto& [name, value] : request.headers) {
            body += "header:  " + name + ": " + value + "\n";
        }
        return HttpResponse::text(http::status::kOk, body);
    });

    router.post("/echo", [](const HttpRequest& request) {
        HttpResponse response(http::status::kOk);
        response.set_body(request.body,
                          request.header("Content-Type").value_or("application/octet-stream"));
        return response;
    });

    return router;
}

void send(http::Socket& client, http::HttpResponse response, bool head_only = false) {
    response.set_header("Connection", "close");
    client.send_all(response.serialize(head_only));
}

void handle_connection(const http::Router& router, http::Socket& client) {
    try {
        const auto request = http::read_request(client);
        if (!request) {
            return;
        }

        http::HttpResponse response = router.route(*request);
        std::cout << to_string(request->method) << ' ' << request->target << " -> "
                  << response.status() << '\n';
        send(client, std::move(response), request->method == http::Method::Head);
    } catch (const http::RequestTooLarge& error) {
        send(client, http::HttpResponse::text(http::status::kPayloadTooLarge,
                                              std::string(error.what()) + "\n"));
    } catch (const http::ParseError& error) {
        send(client, http::HttpResponse::text(http::status::kBadRequest,
                                              std::string(error.what()) + "\n"));
    } catch (const std::exception& error) {
        std::cerr << "handler error: " << error.what() << '\n';
        send(client, http::HttpResponse::text(http::status::kInternalServerError,
                                              "500 Internal Server Error\n"));
    }
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);

    try {
        const http::Router router = build_router();
        http::TcpServer server("0.0.0.0", parse_port(argc, argv));
        server.listen();
        std::cout << http::kServerName << " v" << http::kServerVersion
                  << " listening on http://localhost:" << server.port() << " ("
                  << router.size() << " routes)\n";
        server.serve([&router](http::Socket& client) { handle_connection(router, client); });
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
