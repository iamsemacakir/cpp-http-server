#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

#include "url.hpp"

namespace http {

enum class Method { Get, Head, Post, Put, Patch, Delete, Options, Unknown };

Method method_from_string(std::string_view text) noexcept;
std::string_view to_string(Method method) noexcept;

// Thrown when the incoming bytes are not a valid HTTP request.
class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Thrown when the request head or body exceeds the configured limits.
class RequestTooLarge : public ParseError {
public:
    using ParseError::ParseError;
};

struct HttpRequest {
    Method method = Method::Unknown;
    std::string target;  // raw request target, e.g. "/search?q=cpp"
    std::string path;    // "/search", percent-decoded
    std::string query;   // "q=cpp" (raw, without the '?')
    QueryParams query_params; // decoded key/value pairs from `query`
    std::string version; // "HTTP/1.1"

    // Header names are stored lower-cased, since HTTP headers are
    // case-insensitive.
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    // Path parameters captured by the router, e.g. {"id": "42"} for the
    // route "/users/:id" matching "/users/42".
    std::unordered_map<std::string, std::string> params;

    // Value of a captured path parameter, if present.
    std::optional<std::string> param(std::string_view name) const;

    // First value of a query parameter, if present.
    std::optional<std::string> query_param(std::string_view name) const;

    // Case-insensitive header lookup.
    std::optional<std::string> header(std::string_view name) const;

    // Value of Content-Length, or 0 if absent. Throws ParseError if invalid.
    std::size_t content_length() const;

    // Whether the client wants the connection kept open after the response:
    // the default for HTTP/1.1 unless it sends "Connection: close", and only
    // with an explicit "Connection: keep-alive" for HTTP/1.0.
    bool keep_alive() const;
};

// Parses a request line such as "GET /index.html HTTP/1.1" (no CRLF).
HttpRequest parse_request_line(std::string_view line);

// Parses a full request head: the request line followed by header lines,
// separated by CRLF, without the terminating empty line.
HttpRequest parse_request_head(std::string_view head);

} // namespace http
