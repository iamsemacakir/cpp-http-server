#include "http_request.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <utility>

namespace http {

namespace {

constexpr std::array<std::pair<std::string_view, Method>, 7> kMethods{{
    {"GET", Method::Get},
    {"HEAD", Method::Head},
    {"POST", Method::Post},
    {"PUT", Method::Put},
    {"PATCH", Method::Patch},
    {"DELETE", Method::Delete},
    {"OPTIONS", Method::Options},
}};

std::string to_lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

bool is_token_char(unsigned char c) {
    return std::isalnum(c) || std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) !=
                                  std::string_view::npos;
}

bool is_supported_version(std::string_view version) {
    return version == "HTTP/1.1" || version == "HTTP/1.0";
}

} // namespace

Method method_from_string(std::string_view text) noexcept {
    for (const auto& [name, method] : kMethods) {
        if (name == text) {
            return method;
        }
    }
    return Method::Unknown;
}

std::string_view to_string(Method method) noexcept {
    for (const auto& [name, value] : kMethods) {
        if (value == method) {
            return name;
        }
    }
    return "UNKNOWN";
}

HttpRequest parse_request_line(std::string_view line) {
    const auto first_space = line.find(' ');
    const auto second_space = first_space == std::string_view::npos
                                  ? std::string_view::npos
                                  : line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos ||
        line.find(' ', second_space + 1) != std::string_view::npos) {
        throw ParseError("malformed request line");
    }

    const std::string_view method = line.substr(0, first_space);
    const std::string_view target = line.substr(first_space + 1, second_space - first_space - 1);
    const std::string_view version = line.substr(second_space + 1);

    if (method.empty() || target.empty()) {
        throw ParseError("malformed request line");
    }
    if (target.front() != '/' && target != "*") {
        throw ParseError("request target must start with '/'");
    }
    if (!is_supported_version(version)) {
        throw ParseError("unsupported HTTP version");
    }

    HttpRequest request;
    request.method = method_from_string(method);
    request.target = std::string(target);
    request.version = std::string(version);

    const auto question_mark = target.find('?');
    request.path = std::string(target.substr(0, question_mark));
    if (question_mark != std::string_view::npos) {
        request.query = std::string(target.substr(question_mark + 1));
    }
    return request;
}

std::optional<std::string> HttpRequest::header(std::string_view name) const {
    const auto it = headers.find(to_lower(name));
    if (it == headers.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::size_t HttpRequest::content_length() const {
    const auto value = header("Content-Length");
    if (!value) {
        return 0;
    }
    std::size_t length = 0;
    const char* begin = value->data();
    const char* end = begin + value->size();
    const auto [ptr, error] = std::from_chars(begin, end, length);
    if (value->empty() || error != std::errc{} || ptr != end) {
        throw ParseError("invalid Content-Length");
    }
    return length;
}

HttpRequest parse_request_head(std::string_view head) {
    auto line_end = head.find("\r\n");
    HttpRequest request = parse_request_line(head.substr(0, line_end));

    while (line_end != std::string_view::npos) {
        const auto line_start = line_end + 2;
        line_end = head.find("\r\n", line_start);
        const std::string_view line = head.substr(line_start, line_end - line_start);
        if (line.empty()) {
            continue;
        }

        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            throw ParseError("malformed header line");
        }
        const std::string_view name = line.substr(0, colon);
        if (!std::all_of(name.begin(), name.end(),
                         [](char c) { return is_token_char(static_cast<unsigned char>(c)); })) {
            throw ParseError("invalid header name");
        }

        std::string key = to_lower(name);
        const std::string_view value = trim(line.substr(colon + 1));
        auto [it, inserted] = request.headers.try_emplace(std::move(key), value);
        if (!inserted) {
            // Repeated headers are combined into a comma-separated list.
            it->second.append(", ").append(value);
        }
    }
    return request;
}

} // namespace http
