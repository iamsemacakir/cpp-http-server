#include "http_request.hpp"

#include <array>
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

} // namespace http
