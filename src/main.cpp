#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "config.hpp"
#include "connection.hpp"
#include "error_pages.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "logger.hpp"
#include "router.hpp"
#include "static_files.hpp"
#include "tcp_server.hpp"
#include "thread_pool.hpp"
#include "version.hpp"

namespace {

constexpr const char* kDefaultConfigFile = "server.conf";

std::uint16_t parse_port(const std::string& text) {
    std::size_t consumed = 0;
    const int port = std::stoi(text, &consumed);
    if (consumed != text.size() || port < 0 || port > 65535) {
        throw std::out_of_range("port must be between 0 and 65535");
    }
    return static_cast<std::uint16_t>(port);
}

// Builds the runtime configuration, later sources overriding earlier ones:
//   1. built-in defaults
//   2. the config file: $SERVER_CONFIG if set (must exist), otherwise
//      ./server.conf when present
//   3. LOG_LEVEL=debug|info|warn|error|off from the environment
//   4. positional arguments: [port] [document_root]
http::ServerConfig load_configuration(int argc, char** argv, std::string& config_source) {
    http::ServerConfig config;
    if (const char* path = std::getenv("SERVER_CONFIG"); path != nullptr && *path != '\0') {
        config = http::load_config(path);
        config_source = path;
    } else if (std::filesystem::is_regular_file(kDefaultConfigFile)) {
        config = http::load_config(kDefaultConfigFile);
        config_source = kDefaultConfigFile;
    } else {
        config_source = "defaults";
    }

    if (const char* value = std::getenv("LOG_LEVEL"); value != nullptr) {
        if (const auto level = http::log_level_from_string(value)) {
            config.log_level = *level;
        } else {
            http::log_warn("unknown LOG_LEVEL '", value, "', ignoring it");
        }
    }

    if (argc >= 2) {
        config.port = parse_port(argv[1]);
    }
    if (argc >= 3) {
        config.document_root = argv[2];
    }
    return config;
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

// Routes one request, renders error pages and writes the access log line.
http::HttpResponse handle_request(const http::Router& router,
                                  const http::ErrorPages& error_pages,
                                  const http::HttpRequest& request, const std::string& peer) {
    const auto started = std::chrono::steady_clock::now();
    http::HttpResponse response;
    try {
        response = router.route(request);
    } catch (const std::exception& error) {
        http::log_error("handler error for ", request.target, ": ", error.what());
        response = http::HttpResponse::error(http::status::kInternalServerError);
    }
    error_pages.apply(request, response);

    const std::chrono::duration<double, std::milli> elapsed =
        std::chrono::steady_clock::now() - started;

    // Access log: "127.0.0.1:51234 GET /index.html -> 200 (501 B, 0.21 ms)"
    char duration[32];
    std::snprintf(duration, sizeof(duration), "%.2f ms", elapsed.count());
    http::log_info(peer, ' ', http::to_string(request.method), ' ', request.target, " -> ",
                   response.status(), " (", response.body().size(), " B, ", duration, ')');
    return response;
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);

    try {
        std::string config_source;
        const http::ServerConfig config = load_configuration(argc, argv, config_source);
        http::Logger::instance().set_level(config.log_level);
        std::string effective = http::to_config_string(config);
        effective.pop_back(); // trailing newline
        http::log_debug("effective configuration (", config_source, "):\n", effective);

        const http::StaticFiles static_files(config.document_root);
        const http::Router router = build_router(static_files);
        const http::ErrorPages error_pages(config.error_pages);
        http::TcpServer server(config.host, config.port);
        server.set_client_timeout(config.client_timeout);
        server.listen();

        // Declared after everything the handler uses, so it is destroyed
        // (draining in-flight connections) before the router and pages.
        http::ThreadPool pool(config.threads);
        http::log_info(http::kServerName, " v", http::kServerVersion, " listening on http://",
                       config.host, ':', server.port(), " (config: ", config_source, ", ",
                       router.size(), " routes, serving ", static_files.root().string(), ", ",
                       error_pages.template_count(), " error templates, ", pool.size(),
                       " worker threads)");
        server.serve(
            [&router, &error_pages, &config](http::Socket& client) {
                const std::string peer = client.peer_address();
                http::serve_connection(
                    client,
                    [&](const http::HttpRequest& request) {
                        return handle_request(router, error_pages, request, peer);
                    },
                    config.keep_alive);
            },
            pool);
    } catch (const std::exception& error) {
        http::log_error("fatal: ", error.what());
        return 1;
    }
    return 0;
}
