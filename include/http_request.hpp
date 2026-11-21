#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace http {

enum class Method { Get, Head, Post, Put, Patch, Delete, Options, Unknown };

Method method_from_string(std::string_view text) noexcept;
std::string_view to_string(Method method) noexcept;

// Thrown when the incoming bytes are not a valid HTTP request.
class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct HttpRequest {
    Method method = Method::Unknown;
    std::string target;  // raw request target, e.g. "/search?q=cpp"
    std::string path;    // "/search"
    std::string query;   // "q=cpp" (without the '?')
    std::string version; // "HTTP/1.1"
};

// Parses a request line such as "GET /index.html HTTP/1.1" (no CRLF).
HttpRequest parse_request_line(std::string_view line);

} // namespace http
