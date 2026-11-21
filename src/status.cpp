#include "status.hpp"

#include <array>
#include <utility>

namespace http {

namespace {

constexpr std::array<std::pair<int, std::string_view>, 40> kReasonPhrases{{
    {100, "Continue"},
    {101, "Switching Protocols"},
    {200, "OK"},
    {201, "Created"},
    {202, "Accepted"},
    {203, "Non-Authoritative Information"},
    {204, "No Content"},
    {205, "Reset Content"},
    {206, "Partial Content"},
    {300, "Multiple Choices"},
    {301, "Moved Permanently"},
    {302, "Found"},
    {303, "See Other"},
    {304, "Not Modified"},
    {307, "Temporary Redirect"},
    {308, "Permanent Redirect"},
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {406, "Not Acceptable"},
    {408, "Request Timeout"},
    {409, "Conflict"},
    {410, "Gone"},
    {411, "Length Required"},
    {413, "Payload Too Large"},
    {414, "URI Too Long"},
    {415, "Unsupported Media Type"},
    {416, "Range Not Satisfiable"},
    {418, "I'm a teapot"},
    {422, "Unprocessable Content"},
    {429, "Too Many Requests"},
    {431, "Request Header Fields Too Large"},
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"},
    {504, "Gateway Timeout"},
    {505, "HTTP Version Not Supported"},
}};

} // namespace

std::string_view reason_phrase(int status) noexcept {
    for (const auto& [code, phrase] : kReasonPhrases) {
        if (code == status) {
            return phrase;
        }
    }
    if (is_informational(status)) return "Informational";
    if (is_success(status)) return "Success";
    if (is_redirect(status)) return "Redirection";
    if (is_client_error(status)) return "Client Error";
    if (is_server_error(status)) return "Server Error";
    return "Unknown";
}

bool is_informational(int status) noexcept { return status >= 100 && status < 200; }
bool is_success(int status) noexcept { return status >= 200 && status < 300; }
bool is_redirect(int status) noexcept { return status >= 300 && status < 400; }
bool is_client_error(int status) noexcept { return status >= 400 && status < 500; }
bool is_server_error(int status) noexcept { return status >= 500 && status < 600; }

} // namespace http
