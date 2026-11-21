#pragma once

#include <string_view>

namespace http {

// Content-Type for a file path based on its extension (case-insensitive),
// e.g. "style.css" -> "text/css; charset=utf-8". Unknown extensions map to
// "application/octet-stream".
std::string_view mime_type(std::string_view path) noexcept;

} // namespace http
