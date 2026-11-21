#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace http {

// Builds an HTTP/1.1 response and serializes it to the wire format.
class HttpResponse {
public:
    explicit HttpResponse(int status = 200);

    // Convenience factories for common content types.
    static HttpResponse text(int status, std::string body);
    static HttpResponse html(int status, std::string body);

    int status() const noexcept { return status_; }
    HttpResponse& set_status(int status) noexcept;

    // Replaces any existing header with the same (case-insensitive) name.
    HttpResponse& set_header(std::string name, std::string value);
    std::optional<std::string> header(std::string_view name) const;
    const std::vector<std::pair<std::string, std::string>>& headers() const noexcept {
        return headers_;
    }

    HttpResponse& set_body(std::string body, std::string content_type);
    const std::string& body() const noexcept { return body_; }

    // Status line + headers + body. Adds Content-Length, Date and Server
    // headers automatically. When head_only is true the body is omitted
    // (used for HEAD requests) but Content-Length still reflects it.
    std::string serialize(bool head_only = false) const;

private:
    int status_;
    std::vector<std::pair<std::string, std::string>> headers_;
    std::string body_;
};

} // namespace http
