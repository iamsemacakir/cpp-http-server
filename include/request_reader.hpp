#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "http_request.hpp"
#include "socket.hpp"

namespace http {

struct RequestLimits {
    std::size_t max_head_bytes = 8 * 1024;
    std::size_t max_body_bytes = 1024 * 1024;
};

// Reads consecutive HTTP requests (head + body) from one connection.
// Bytes received past the end of a request are kept for the next call, so
// pipelined requests on a keep-alive connection are not lost.
class RequestReader {
public:
    explicit RequestReader(const Socket& socket, RequestLimits limits = {});

    // Reads the next complete request. Returns std::nullopt if the client
    // closed the connection, or went idle (socket timeout), before sending
    // the first byte of a new request. Throws ParseError / RequestTooLarge
    // for invalid requests and std::system_error on a timeout mid-request.
    std::optional<HttpRequest> next();

private:
    // Appends one chunk from the socket; returns false on end of stream.
    bool fill();

    const Socket* socket_;
    RequestLimits limits_;
    std::string buffer_;
};

} // namespace http
