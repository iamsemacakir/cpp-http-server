#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>

#include "error_pages.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "logger.hpp"
#include "request_reader.hpp"
#include "router.hpp"
#include "static_files.hpp"
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

// LOG_LEVEL=debug|info|warn|error|off (default: info).
void configure_logging() {
    const char* value = std::getenv("LOG_LEVEL");
    if (value == nullptr) {
        return;
    }
    if (const auto level = http::log_level_from_string(value)) {
        http::Logger::instance().set_level(*level);
    } else {
        http::log_warn("unknown LOG_LEVEL '", value, "', using info");
    }
}

http::Router build_router(const http::StaticFiles& static_files) {
    using http::HttpRequest;
    using http::HttpResponse;

    http::Router router;

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
        for (const auto& [key, value] : request.query_params) {
            body += "param:   " + key + " = " + value + "\n";
        }
        for (const auto& [name, value] : request.headers) {
            body += "header:  " + name + ": " + value + "\n";
        }
        return HttpResponse::text(http::status::kOk, body);
    });

    router.get("/search", [](const HttpRequest& request) {
        const auto query = request.query_param("q");
        if (!query || query->empty()) {
            return HttpResponse::text(http::status::kBadRequest,
                                      "missing query parameter 'q'\n");
        }
        const std::string limit = request.query_param("limit").value_or("10");
        return HttpResponse::text(http::status::kOk, "Searching for \"" + *query +
                                                         "\" (limit " + limit + ")\n");
    });

    router.get("/users/:id", [](const HttpRequest& request) {
        return HttpResponse::text(http::status::kOk,
                                  "User #" + request.param("id").value_or("") + "\n");
    });

    router.get("/users/:id/posts/:post_id", [](const HttpRequest& request) {
        return HttpResponse::text(http::status::kOk,
                                  "Post #" + request.param("post_id").value_or("") +
                                      " by user #" + request.param("id").value_or("") + "\n");
    });

    router.get("/error", [](const HttpRequest&) -> HttpResponse {
        throw std::runtime_error("demo failure from /error");
    });

    router.post("/echo", [](const HttpRequest& request) {
        HttpResponse response(http::status::kOk);
        response.set_body(request.body,
                          request.header("Content-Type").value_or("application/octet-stream"));
        return response;
    });

    router.fallback([&static_files](const HttpRequest& request) {
        return static_files.serve(request);
    });

    return router;
}

void send(http::Socket& client, http::HttpResponse response, bool head_only = false) {
    response.set_header("Connection", "close");
    client.send_all(response.serialize(head_only));
}

void handle_connection(const http::Router& router, const http::ErrorPages& error_pages,
                       http::Socket& client) {
    const std::string peer = client.peer_address();
    std::optional<http::HttpRequest> request;
    try {
        request = http::read_request(client);
    } catch (const http::RequestTooLarge& error) {
        http::log_warn(peer, " request too large: ", error.what());
        send(client, http::HttpResponse::error(http::status::kPayloadTooLarge, error.what()));
        return;
    } catch (const http::ParseError& error) {
        http::log_warn(peer, " bad request: ", error.what());
        send(client, http::HttpResponse::error(http::status::kBadRequest, error.what()));
        return;
    }
    if (!request) {
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    http::HttpResponse response;
    try {
        response = router.route(*request);
    } catch (const std::exception& error) {
        http::log_error("handler error for ", request->target, ": ", error.what());
        response = http::HttpResponse::error(http::status::kInternalServerError);
    }
    error_pages.apply(*request, response);

    const std::chrono::duration<double, std::milli> elapsed =
        std::chrono::steady_clock::now() - started;

    // Access log: "127.0.0.1:51234 GET /index.html -> 200 (501 B, 0.21 ms)"
    char duration[32];
    std::snprintf(duration, sizeof(duration), "%.2f ms", elapsed.count());
    http::log_info(peer, ' ', http::to_string(request->method), ' ', request->target, " -> ",
                   response.status(), " (", response.body().size(), " B, ", duration, ')');
    send(client, std::move(response), request->method == http::Method::Head);
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    configure_logging();

    try {
        const http::StaticFiles static_files(argc >= 3 ? argv[2] : "public");
        const http::Router router = build_router(static_files);
        const http::ErrorPages error_pages("errors");
        http::TcpServer server("0.0.0.0", parse_port(argc, argv));
        server.listen();
        http::log_info(http::kServerName, " v", http::kServerVersion,
                       " listening on http://localhost:", server.port(), " (", router.size(),
                       " routes, serving ", static_files.root().string(), ", ",
                       error_pages.template_count(), " error templates)");
        server.serve([&router, &error_pages](http::Socket& client) {
            handle_connection(router, error_pages, client);
        });
    } catch (const std::exception& error) {
        http::log_error("fatal: ", error.what());
        return 1;
    }
    return 0;
}
