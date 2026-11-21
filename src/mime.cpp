#include "mime.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace http {

namespace {

constexpr std::array<std::pair<std::string_view, std::string_view>, 27> kMimeTypes{{
    {"html", "text/html; charset=utf-8"},
    {"htm", "text/html; charset=utf-8"},
    {"css", "text/css; charset=utf-8"},
    {"js", "text/javascript; charset=utf-8"},
    {"mjs", "text/javascript; charset=utf-8"},
    {"json", "application/json"},
    {"txt", "text/plain; charset=utf-8"},
    {"md", "text/markdown; charset=utf-8"},
    {"csv", "text/csv; charset=utf-8"},
    {"xml", "application/xml"},
    {"svg", "image/svg+xml"},
    {"png", "image/png"},
    {"jpg", "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"gif", "image/gif"},
    {"webp", "image/webp"},
    {"ico", "image/x-icon"},
    {"avif", "image/avif"},
    {"woff", "font/woff"},
    {"woff2", "font/woff2"},
    {"ttf", "font/ttf"},
    {"otf", "font/otf"},
    {"pdf", "application/pdf"},
    {"zip", "application/zip"},
    {"wasm", "application/wasm"},
    {"mp4", "video/mp4"},
    {"mp3", "audio/mpeg"},
}};

constexpr std::string_view kDefaultType = "application/octet-stream";

bool iequals(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

std::string_view mime_type(std::string_view path) noexcept {
    const auto slash = path.find_last_of('/');
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 == name.size()) {
        return kDefaultType;
    }
    const std::string_view extension = name.substr(dot + 1);
    for (const auto& [ext, type] : kMimeTypes) {
        if (iequals(ext, extension)) {
            return type;
        }
    }
    return kDefaultType;
}

} // namespace http
