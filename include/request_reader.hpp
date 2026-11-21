#pragma once

#include <cstddef>
#include <optional>

#include "http_request.hpp"
#include "socket.hpp"

namespace http {

struct RequestLimits {
    std::size_t max_head_bytes = 8 * 1024;
    std::size_t max_body_bytes = 1024 * 1024;
};

// Reads one complete HTTP request (head + body) from the socket.
// Returns std::nullopt if the client closed the connection before sending
// anything. Throws ParseError / RequestTooLarge for invalid requests.
std::optional<HttpRequest> read_request(const Socket& socket,
                                        const RequestLimits& limits = {});

} // namespace http
