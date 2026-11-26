#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "json.hpp"
#include "status.hpp"

namespace http {

// Builds an HTTP/1.1 response and serializes it to the wire format.
class HttpResponse {
public:
    explicit HttpResponse(int status = status::kOk);

    // Convenience factories for common content types.
    static HttpResponse text(int status, std::string body);
    static HttpResponse html(int status, std::string body);

    // A JSON body with Content-Type application/json. `indent` is passed to
    // Json::dump(): compact by default; a trailing newline is always added.
    static HttpResponse json(int status, const Json& body, int indent = -1);

    // A JSON error body: {"error":{"status":404,"message":"..."}}. The
    // message defaults to the status code's reason phrase.
    static HttpResponse json_error(int status, std::string message = {});

    // A server-generated error, e.g. error(404, "/missing"). The body is
    // plain text ("404 Not Found: /missing"), but the response is marked so
    // ErrorPages can replace it with an HTML page later.
    static HttpResponse error(int status, std::string detail = {});

    bool is_generated_error() const noexcept { return error_detail_.has_value(); }
    const std::string& error_detail() const noexcept;

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
    std::optional<std::string> error_detail_;
};

} // namespace http
