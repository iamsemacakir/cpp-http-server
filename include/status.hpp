#pragma once

#include <string_view>

namespace http {

// Named constants for the status codes the server uses.
namespace status {
constexpr int kOk = 200;
constexpr int kCreated = 201;
constexpr int kNoContent = 204;
constexpr int kMovedPermanently = 301;
constexpr int kFound = 302;
constexpr int kNotModified = 304;
constexpr int kBadRequest = 400;
constexpr int kUnauthorized = 401;
constexpr int kForbidden = 403;
constexpr int kNotFound = 404;
constexpr int kMethodNotAllowed = 405;
constexpr int kRequestTimeout = 408;
constexpr int kPayloadTooLarge = 413;
constexpr int kUriTooLong = 414;
constexpr int kUnsupportedMediaType = 415;
constexpr int kTooManyRequests = 429;
constexpr int kRequestHeaderFieldsTooLarge = 431;
constexpr int kInternalServerError = 500;
constexpr int kNotImplemented = 501;
constexpr int kServiceUnavailable = 503;
constexpr int kHttpVersionNotSupported = 505;
} // namespace status

// Standard reason phrase for a status code (RFC 9110), e.g. 404 -> "Not Found".
// Unknown codes get a generic phrase based on their class.
std::string_view reason_phrase(int status) noexcept;

bool is_informational(int status) noexcept;
bool is_success(int status) noexcept;
bool is_redirect(int status) noexcept;
bool is_client_error(int status) noexcept;
bool is_server_error(int status) noexcept;

} // namespace http
