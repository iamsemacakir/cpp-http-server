#include "url.hpp"

#include <cctype>

namespace http {

namespace {

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool is_unreserved(unsigned char c) noexcept {
    return std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

} // namespace

std::optional<std::string> url_decode(std::string_view text, bool plus_as_space) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '%') {
            if (i + 2 >= text.size()) {
                return std::nullopt;
            }
            const int high = hex_value(text[i + 1]);
            const int low = hex_value(text[i + 2]);
            if (high < 0 || low < 0) {
                return std::nullopt;
            }
            result += static_cast<char>(high * 16 + low);
            i += 2;
        } else if (c == '+' && plus_as_space) {
            result += ' ';
        } else {
            result += c;
        }
    }
    return result;
}

std::string url_encode(std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(text.size());
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (is_unreserved(byte)) {
            result += c;
        } else {
            result += '%';
            result += kHex[byte >> 4];
            result += kHex[byte & 0x0F];
        }
    }
    return result;
}

std::optional<QueryParams> parse_query(std::string_view query) {
    QueryParams params;
    while (!query.empty()) {
        const auto amp = query.find('&');
        const std::string_view pair = query.substr(0, amp);
        query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
        if (pair.empty()) {
            continue;
        }

        const auto equals = pair.find('=');
        auto key = url_decode(pair.substr(0, equals), true);
        auto value = equals == std::string_view::npos ? std::optional<std::string>{""}
                                                      : url_decode(pair.substr(equals + 1), true);
        if (!key || !value) {
            return std::nullopt;
        }
        params.emplace(std::move(*key), std::move(*value));
    }
    return params;
}

} // namespace http
