#include "error_pages.hpp"

#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

#include "status.hpp"

namespace http {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kBuiltinTemplate = R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>{{status}} {{reason}}</title>
  <style>
    body { font-family: system-ui, sans-serif; text-align: center; margin: 4rem 1rem; color: #333; }
    h1 { font-size: 3rem; margin-bottom: 0.5rem; }
    p { color: #666; }
  </style>
</head>
<body>
  <h1>{{status}}</h1>
  <h2>{{reason}}</h2>
  <p>{{message}}</p>
  <hr>
  <small>cpp-http-server</small>
</body>
</html>
)";

// Parses "404.html" -> 404; anything else -> 0.
int status_from_filename(const std::string& name) {
    if (name.size() != 8 || name.compare(3, 5, ".html") != 0) {
        return 0;
    }
    int status = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
            return 0;
        }
        status = status * 10 + (name[i] - '0');
    }
    return status >= 400 && status <= 599 ? status : 0;
}

void replace_all(std::string& text, std::string_view from, std::string_view to) {
    for (auto pos = text.find(from); pos != std::string::npos;
         pos = text.find(from, pos + to.size())) {
        text.replace(pos, from.size(), to);
    }
}

bool accepts_html(const HttpRequest& request) {
    const auto accept = request.header("Accept");
    return accept && accept->find("text/html") != std::string::npos;
}

} // namespace

std::string html_escape(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result += c; break;
        }
    }
    return result;
}

ErrorPages::ErrorPages(const fs::path& directory) {
    std::error_code error;
    if (!fs::is_directory(directory, error)) {
        return;
    }
    for (const auto& entry : fs::directory_iterator(directory, error)) {
        const int status = status_from_filename(entry.path().filename().string());
        if (status == 0 || !entry.is_regular_file(error)) {
            continue;
        }
        std::ifstream input(entry.path(), std::ios::binary);
        if (input) {
            templates_[status].assign(std::istreambuf_iterator<char>(input),
                                      std::istreambuf_iterator<char>());
        }
    }
}

std::string ErrorPages::render(int status, std::string_view message) const {
    const auto it = templates_.find(status);
    std::string page = it != templates_.end() ? it->second : std::string(kBuiltinTemplate);
    replace_all(page, "{{status}}", std::to_string(status));
    replace_all(page, "{{reason}}", html_escape(reason_phrase(status)));
    replace_all(page, "{{message}}", html_escape(message));
    return page;
}

void ErrorPages::apply(const HttpRequest& request, HttpResponse& response) const {
    const int status = response.status();
    if (!response.is_generated_error() || !(is_client_error(status) || is_server_error(status)) ||
        !accepts_html(request)) {
        return;
    }
    response.set_body(render(status, response.error_detail()), "text/html; charset=utf-8");
}

} // namespace http
