#pragma once

#include <chrono>
#include <cstddef>
#include <functional>

#include "http_request.hpp"
#include "http_response.hpp"
#include "request_reader.hpp"
#include "socket.hpp"
#include "stop_signal.hpp"

namespace http {

struct KeepAliveOptions {
    bool enabled = true;
    // How long an open connection may sit idle waiting for the next request.
    std::chrono::milliseconds idle_timeout{5'000};
    // Requests served on one connection before the server closes it.
    std::size_t max_requests = 100;
};

// Turns a parsed request into a response (routing, error pages, logging).
using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;

// Serves HTTP requests on one client connection. With keep-alive enabled the
// connection stays open for further requests until the client asks to close
// it, goes idle for longer than idle_timeout, hits max_requests, or sends a
// malformed request. Every response carries a matching Connection header.
//
// When `stop` is given and gets requested (graceful shutdown), a connection
// waiting for its next request is closed right away, while a request that is
// already being received or handled is completed and answered with
// `Connection: close`. Returns the number of requests answered.
std::size_t serve_connection(Socket& client, const RequestHandler& handler,
                             const KeepAliveOptions& keep_alive = {},
                             const RequestLimits& limits = {},
                             const StopSignal* stop = nullptr);

} // namespace http
