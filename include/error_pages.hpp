#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

#include "http_request.hpp"
#include "http_response.hpp"

namespace http {

// Turns server-generated errors (HttpResponse::error) into HTML pages for
// clients that accept HTML. Non-HTML clients such as curl keep the short
// plain-text body.
//
// Templates are loaded once from a directory, named by status code
// ("404.html", "500.html", ...). They may use the placeholders {{status}},
// {{reason}} and {{message}} (HTML-escaped). Status codes without a template
// get a built-in page.
class ErrorPages {
public:
    ErrorPages() = default;

    // Loads every "<3-digit status>.html" file in `directory`. A missing
    // directory is not an error: only the built-in page is used then.
    explicit ErrorPages(const std::filesystem::path& directory);

    // Replaces the body of `response` when it is a generated 4xx/5xx error
    // and the request accepts text/html. Headers (Allow, ...) are kept.
    void apply(const HttpRequest& request, HttpResponse& response) const;

    // HTML for a status code, using a loaded template or the built-in page.
    std::string render(int status, std::string_view message) const;

    std::size_t template_count() const noexcept { return templates_.size(); }

private:
    std::map<int, std::string> templates_;
};

// Escapes &, <, >, " and ' for safe inclusion in HTML.
std::string html_escape(std::string_view text);

} // namespace http
