#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "cli.hpp"
#include "config.hpp"
#include "connection.hpp"
#include "demo_api.hpp"
#include "error_pages.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "json.hpp"
#include "logger.hpp"
#include "router.hpp"
#include "signal_watcher.hpp"
#include "static_files.hpp"
#include "tcp_server.hpp"
#include "thread_pool.hpp"
#include "version.hpp"

namespace {

constexpr const char* kDefaultConfigFile = "server.conf";

// Builds the runtime configuration, later sources overriding earlier ones:
//   1. built-in defaults
//   2. the config file: --config FILE, else $SERVER_CONFIG if set (both must
//      exist), otherwise ./server.conf when present
//   3. LOG_LEVEL=debug|info|warn|error|off from the environment
//   4. command-line options (see `http_server --help`)
http::ServerConfig load_configuration(const http::CliOptions& options,
                                      std::string& config_source) {
    http::ServerConfig config;
    if (options.config_file) {
        config = http::load_config(*options.config_file);
        config_source = options.config_file->string();
    } else if (const char* path = std::getenv("SERVER_CONFIG");
               path != nullptr && *path != '\0') {
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

    http::apply_cli(options, config);
    return config;
}

http::Router build_router(const http::StaticFiles& static_files, http::TodoStore& todos) {
    using http::HttpRequest;
    using http::HttpResponse;

    http::Router router;

    router.get("/hello", [](const HttpRequest&) {
        return HttpResponse::text(http::status::kOk, "Hello from cpp-http-server!\n");
    });

    router.get("/health", [](const HttpRequest&) {
        return HttpResponse::json(http::status::kOk, http::Json::object({{"status", "ok"}}));
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

    // JSON API under /api (see demo_api.hpp).
    http::register_demo_api(router, todos);

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
    const char* program = argc > 0 ? argv[0] : http::kServerName;

    http::CliOptions options;
    try {
        options = http::parse_cli(argc, argv);
    } catch (const http::CliError& error) {
        std::cerr << program << ": " << error.what() << '\n'
                  << "Try '" << program << " --help' for more information.\n";
        return 2;
    }
    if (options.show_help) {
        std::cout << http::cli_usage(program);
        return 0;
    }
    if (options.show_version) {
        std::cout << http::kServerName << ' ' << http::kServerVersion << '\n';
        return 0;
    }

    try {
        std::string config_source;
        const http::ServerConfig config = load_configuration(options, config_source);
        if (options.print_config) {
            std::cout << "# effective configuration (" << config_source << ")\n"
                      << http::to_config_string(config);
            return 0;
        }
        http::Logger::instance().set_level(config.log_level);
        std::string effective = http::to_config_string(config);
        effective.pop_back(); // trailing newline
        http::log_debug("effective configuration (", config_source, "):\n", effective);

        const http::StaticFiles static_files(config.document_root);
        http::TodoStore todos;
        todos.create("Read the ROADMAP", true);
        todos.create("Try the JSON API with curl");
        const http::Router router = build_router(static_files, todos);
        const http::ErrorPages error_pages(config.error_pages);
        http::TcpServer server(config.host, config.port);
        server.set_client_timeout(config.client_timeout);
        server.listen();

        // SIGINT (Ctrl+C) and SIGTERM (docker stop, systemd, kill) stop the
        // accept loop; a second signal exits immediately. Created before the
        // pool so the worker threads inherit the blocked signal mask.
        const http::SignalWatcher signals({SIGINT, SIGTERM}, [&server](int signal) {
            http::log_info("received ", http::signal_name(signal),
                           ", shutting down gracefully (", server.active_connections(),
                           " active connection(s), send it again to force quit)");
            server.stop();
        });

        // Declared after everything the handler uses, so it is destroyed
        // (draining in-flight connections) before the router and pages.
        http::ThreadPool pool(config.threads);
        http::log_info(http::kServerName, " v", http::kServerVersion, " listening on http://",
                       config.host, ':', server.port(), " (config: ", config_source, ", ",
                       router.size(), " routes, serving ", static_files.root().string(), ", ",
                       error_pages.template_count(), " error templates, ", pool.size(),
                       " worker threads)");
        server.serve(
            [&router, &error_pages, &config, &server](http::Socket& client) {
                const std::string peer = client.peer_address();
                http::serve_connection(
                    client,
                    [&](const http::HttpRequest& request) {
                        return handle_request(router, error_pages, request, peer);
                    },
                    config.keep_alive, {}, &server.stop_signal());
            },
            pool);

        // The listener is closed now. Idle keep-alive connections wake up and
        // close at once; requests in progress are finished before we exit.
        const auto drain_started = std::chrono::steady_clock::now();
        if (const std::size_t active = server.active_connections(); active > 0) {
            http::log_info("waiting for ", active, " connection(s) to finish");
        }
        pool.shutdown();
        const std::chrono::duration<double, std::milli> drained =
            std::chrono::steady_clock::now() - drain_started;
        char duration[32];
        std::snprintf(duration, sizeof(duration), "%.0f ms", drained.count());
        http::log_info("server stopped (drained in ", duration, ")");
    } catch (const std::exception& error) {
        http::log_error("fatal: ", error.what());
        return 1;
    }
    return 0;
}
