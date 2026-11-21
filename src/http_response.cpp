#include "http_response.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>

#include "version.hpp"

namespace http {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

std::string_view reason_phrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    default: return "Unknown";
    }
}

// RFC 7231 IMF-fixdate, e.g. "Sat, 21 Nov 2026 14:00:00 GMT".
std::string http_date() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    char buffer[64];
    const std::size_t length =
        std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &utc);
    return std::string(buffer, length);
}

} // namespace

HttpResponse::HttpResponse(int status) : status_(status) {}

HttpResponse HttpResponse::text(int status, std::string body) {
    HttpResponse response(status);
    response.set_body(std::move(body), "text/plain; charset=utf-8");
    return response;
}

HttpResponse HttpResponse::html(int status, std::string body) {
    HttpResponse response(status);
    response.set_body(std::move(body), "text/html; charset=utf-8");
    return response;
}

HttpResponse& HttpResponse::set_status(int status) noexcept {
    status_ = status;
    return *this;
}

HttpResponse& HttpResponse::set_header(std::string name, std::string value) {
    const auto it = std::find_if(headers_.begin(), headers_.end(),
                                 [&](const auto& header) { return iequals(header.first, name); });
    if (it != headers_.end()) {
        it->second = std::move(value);
    } else {
        headers_.emplace_back(std::move(name), std::move(value));
    }
    return *this;
}

std::optional<std::string> HttpResponse::header(std::string_view name) const {
    const auto it = std::find_if(headers_.begin(), headers_.end(),
                                 [&](const auto& header) { return iequals(header.first, name); });
    if (it == headers_.end()) {
        return std::nullopt;
    }
    return it->second;
}

HttpResponse& HttpResponse::set_body(std::string body, std::string content_type) {
    body_ = std::move(body);
    set_header("Content-Type", std::move(content_type));
    return *this;
}

std::string HttpResponse::serialize(bool head_only) const {
    std::string out;
    out.reserve(256 + body_.size());
    out += "HTTP/1.1 ";
    out += std::to_string(status_);
    out += ' ';
    out += reason_phrase(status_);
    out += "\r\n";

    const auto append_header = [&out](std::string_view name, std::string_view value) {
        out.append(name).append(": ").append(value).append("\r\n");
    };

    if (!header("Date")) {
        append_header("Date", http_date());
    }
    if (!header("Server")) {
        append_header("Server", std::string(kServerName) + "/" + kServerVersion);
    }
    for (const auto& [name, value] : headers_) {
        append_header(name, value);
    }
    if (!header("Content-Length")) {
        append_header("Content-Length", std::to_string(body_.size()));
    }
    out += "\r\n";

    if (!head_only) {
        out += body_;
    }
    return out;
}

} // namespace http
