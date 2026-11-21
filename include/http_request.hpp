#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

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
    std::string path;    // "/search"
    std::string query;   // "q=cpp" (without the '?')
    std::string version; // "HTTP/1.1"

    // Header names are stored lower-cased, since HTTP headers are
    // case-insensitive.
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    // Case-insensitive header lookup.
    std::optional<std::string> header(std::string_view name) const;

    // Value of Content-Length, or 0 if absent. Throws ParseError if invalid.
    std::size_t content_length() const;
};

// Parses a request line such as "GET /index.html HTTP/1.1" (no CRLF).
HttpRequest parse_request_line(std::string_view line);

// Parses a full request head: the request line followed by header lines,
// separated by CRLF, without the terminating empty line.
HttpRequest parse_request_head(std::string_view head);

} // namespace http
