#include "json.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

namespace http {

namespace {

constexpr int kMaxDepth = 64; // nesting limit, protects the parser's stack

[[noreturn]] void type_error(std::string_view expected, Json::Type actual) {
    throw JsonError("JSON value is " + std::string(to_string(actual)) + ", expected " +
                    std::string(expected));
}

// Shortest representation that reads back as the same double.
std::string format_double(double value) {
    char buffer[32];
    for (const int precision : {15, 16, 17}) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
        if (std::strtod(buffer, nullptr) == value) {
            break;
        }
    }
    std::string text(buffer);
    // "3" would read back as an integer; keep the value a floating-point one.
    if (text.find_first_of(".eE") == std::string::npos) {
        text += ".0";
    }
    return text;
}

void append_utf8(std::string& out, std::uint32_t code_point) {
    if (code_point < 0x80) {
        out += static_cast<char>(code_point);
    } else if (code_point < 0x800) {
        out += static_cast<char>(0xC0 | (code_point >> 6));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else if (code_point < 0x10000) {
        out += static_cast<char>(0xE0 | (code_point >> 12));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code_point >> 18));
        out += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    }
}

void newline(std::string& out, int indent, int depth) {
    if (indent >= 0) {
        out += '\n';
        out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth), ' ');
    }
}

// Recursive-descent parser over the input text.
class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Json parse_document() {
        Json value = parse_value(0);
        skip_whitespace();
        if (pos_ != text_.size()) {
            fail("unexpected trailing characters");
        }
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw JsonError("invalid JSON at offset " + std::to_string(pos_) + ": " + message);
    }

    bool at_end() const noexcept { return pos_ >= text_.size(); }
    char peek() const noexcept { return at_end() ? '\0' : text_[pos_]; }

    void skip_whitespace() noexcept {
        while (!at_end()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                break;
            }
            ++pos_;
        }
    }

    void expect(char c) {
        skip_whitespace();
        if (peek() != c) {
            fail(std::string("expected '") + c + "'");
        }
        ++pos_;
    }

    void expect_literal(std::string_view literal) {
        if (text_.substr(pos_, literal.size()) != literal) {
            fail("invalid literal");
        }
        pos_ += literal.size();
    }

    Json parse_value(int depth) {
        if (depth > kMaxDepth) {
            fail("nesting too deep");
        }
        skip_whitespace();
        switch (peek()) {
        case '{': return parse_object(depth);
        case '[': return parse_array(depth);
        case '"': return Json(parse_string());
        case 't': expect_literal("true"); return Json(true);
        case 'f': expect_literal("false"); return Json(false);
        case 'n': expect_literal("null"); return Json(nullptr);
        default:
            if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
                return parse_number();
            }
            fail(at_end() ? "unexpected end of input" : "unexpected character");
        }
    }

    Json parse_object(int depth) {
        ++pos_; // '{'
        Json::Object members;
        skip_whitespace();
        if (peek() == '}') {
            ++pos_;
            return Json(std::move(members));
        }
        while (true) {
            skip_whitespace();
            if (peek() != '"') {
                fail("expected a string key");
            }
            std::string key = parse_string();
            expect(':');
            Json value = parse_value(depth + 1);
            // Duplicate keys: the last one wins, like most parsers.
            auto it = std::find_if(members.begin(), members.end(),
                                   [&key](const Json::Member& m) { return m.first == key; });
            if (it != members.end()) {
                it->second = std::move(value);
            } else {
                members.emplace_back(std::move(key), std::move(value));
            }
            skip_whitespace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                return Json(std::move(members));
            }
            fail("expected ',' or '}'");
        }
    }

    Json parse_array(int depth) {
        ++pos_; // '['
        Json::Array items;
        skip_whitespace();
        if (peek() == ']') {
            ++pos_;
            return Json(std::move(items));
        }
        while (true) {
            items.push_back(parse_value(depth + 1));
            skip_whitespace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                return Json(std::move(items));
            }
            fail("expected ',' or ']'");
        }
    }

    std::uint32_t parse_hex4() {
        if (pos_ + 4 > text_.size()) {
            fail("truncated \\u escape");
        }
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                --pos_;
                fail("invalid hex digit in \\u escape");
            }
        }
        return value;
    }

    std::string parse_string() {
        ++pos_; // opening quote
        std::string out;
        while (true) {
            if (at_end()) {
                fail("unterminated string");
            }
            const char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                --pos_;
                fail("control character in string");
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_end()) {
                fail("unterminated string");
            }
            const char escape = text_[pos_++];
            switch (escape) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t code_point = parse_hex4();
                if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                    // High surrogate: must be followed by \uDC00-\uDFFF.
                    if (text_.substr(pos_, 2) != "\\u") {
                        fail("unpaired surrogate in \\u escape");
                    }
                    pos_ += 2;
                    const std::uint32_t low = parse_hex4();
                    if (low < 0xDC00 || low > 0xDFFF) {
                        fail("invalid low surrogate in \\u escape");
                    }
                    code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
                    fail("unpaired surrogate in \\u escape");
                }
                append_utf8(out, code_point);
                break;
            }
            default:
                --pos_;
                fail("invalid escape sequence");
            }
        }
    }

    Json parse_number() {
        const std::size_t start = pos_;
        bool is_integer = true;
        const auto digits = [this] {
            const std::size_t first = pos_;
            while (!at_end() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
            return pos_ - first;
        };

        if (peek() == '-') {
            ++pos_;
        }
        if (peek() == '0') {
            ++pos_; // no leading zeros: "012" stops after the 0
        } else if (digits() == 0) {
            fail("invalid number");
        }
        if (peek() == '.') {
            is_integer = false;
            ++pos_;
            if (digits() == 0) {
                fail("expected digits after '.'");
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            is_integer = false;
            ++pos_;
            if (peek() == '+' || peek() == '-') {
                ++pos_;
            }
            if (digits() == 0) {
                fail("expected digits in exponent");
            }
        }

        const std::string number(text_.substr(start, pos_ - start));
        errno = 0;
        if (is_integer) {
            const long long value = std::strtoll(number.c_str(), nullptr, 10);
            if (errno != ERANGE) {
                return Json(static_cast<std::int64_t>(value));
            }
            errno = 0; // too big for int64: fall back to a double
        }
        const double value = std::strtod(number.c_str(), nullptr);
        if (errno == ERANGE && std::isinf(value)) {
            pos_ = start;
            fail("number out of range");
        }
        return Json(value);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

} // namespace

std::string_view to_string(Json::Type type) noexcept {
    switch (type) {
    case Json::Type::Null: return "null";
    case Json::Type::Bool: return "boolean";
    case Json::Type::Integer:
    case Json::Type::Double: return "number";
    case Json::Type::String: return "string";
    case Json::Type::Array: return "array";
    case Json::Type::Object: return "object";
    }
    return "unknown";
}

void append_json_string(std::string& out, std::string_view text) {
    static constexpr char kHex[] = "0123456789abcdef";
    out += '"';
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                const auto byte = static_cast<unsigned char>(c);
                out += "\\u00";
                out += kHex[byte >> 4];
                out += kHex[byte & 0x0F];
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

Json Json::parse(std::string_view text) {
    return Parser(text).parse_document();
}

bool Json::as_bool() const {
    if (!is_bool()) {
        type_error("boolean", type());
    }
    return std::get<bool>(value_);
}

std::int64_t Json::as_integer() const {
    if (is_integer()) {
        return std::get<std::int64_t>(value_);
    }
    if (type() == Type::Double) {
        const double value = std::get<double>(value_);
        // 2^63 is exactly representable; anything at or above it overflows.
        constexpr double kLimit = 9223372036854775808.0;
        if (std::trunc(value) == value && value >= -kLimit && value < kLimit) {
            return static_cast<std::int64_t>(value);
        }
        throw JsonError("JSON number is not an integer");
    }
    type_error("integer", type());
}

double Json::as_double() const {
    if (is_integer()) {
        return static_cast<double>(std::get<std::int64_t>(value_));
    }
    if (type() != Type::Double) {
        type_error("number", type());
    }
    return std::get<double>(value_);
}

const std::string& Json::as_string() const {
    if (!is_string()) {
        type_error("string", type());
    }
    return std::get<std::string>(value_);
}

const Json::Array& Json::as_array() const {
    if (!is_array()) {
        type_error("array", type());
    }
    return std::get<Array>(value_);
}

Json::Array& Json::as_array() {
    if (!is_array()) {
        type_error("array", type());
    }
    return std::get<Array>(value_);
}

const Json::Object& Json::as_object() const {
    if (!is_object()) {
        type_error("object", type());
    }
    return std::get<Object>(value_);
}

Json::Object& Json::as_object() {
    if (!is_object()) {
        type_error("object", type());
    }
    return std::get<Object>(value_);
}

const Json* Json::find(std::string_view key) const noexcept {
    if (!is_object()) {
        return nullptr;
    }
    for (const auto& [name, value] : std::get<Object>(value_)) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

const Json& Json::at(std::string_view key) const {
    if (const Json* value = find(key)) {
        return *value;
    }
    if (!is_object()) {
        type_error("object", type());
    }
    throw JsonError("missing JSON member '" + std::string(key) + "'");
}

Json& Json::operator[](std::string_view key) {
    if (is_null()) {
        value_ = Object{};
    }
    Object& members = as_object();
    for (auto& [name, value] : members) {
        if (name == key) {
            return value;
        }
    }
    members.emplace_back(std::string(key), Json());
    return members.back().second;
}

Json& Json::push_back(Json value) {
    if (is_null()) {
        value_ = Array{};
    }
    Array& items = as_array();
    items.push_back(std::move(value));
    return items.back();
}

std::size_t Json::size() const noexcept {
    if (is_array()) {
        return std::get<Array>(value_).size();
    }
    if (is_object()) {
        return std::get<Object>(value_).size();
    }
    return 0;
}

std::string Json::dump(int indent) const {
    std::string out;
    dump_to(out, indent, 0);
    return out;
}

void Json::dump_to(std::string& out, int indent, int depth) const {
    switch (type()) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += std::get<bool>(value_) ? "true" : "false"; break;
    case Type::Integer: out += std::to_string(std::get<std::int64_t>(value_)); break;
    case Type::Double: {
        const double value = std::get<double>(value_);
        out += std::isfinite(value) ? format_double(value) : "null";
        break;
    }
    case Type::String: append_json_string(out, std::get<std::string>(value_)); break;
    case Type::Array: {
        const Array& items = std::get<Array>(value_);
        out += '[';
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i > 0) {
                out += ',';
            }
            newline(out, indent, depth + 1);
            items[i].dump_to(out, indent, depth + 1);
        }
        if (!items.empty()) {
            newline(out, indent, depth);
        }
        out += ']';
        break;
    }
    case Type::Object: {
        const Object& members = std::get<Object>(value_);
        out += '{';
        for (std::size_t i = 0; i < members.size(); ++i) {
            if (i > 0) {
                out += ',';
            }
            newline(out, indent, depth + 1);
            append_json_string(out, members[i].first);
            out += indent >= 0 ? ": " : ":";
            members[i].second.dump_to(out, indent, depth + 1);
        }
        if (!members.empty()) {
            newline(out, indent, depth);
        }
        out += '}';
        break;
    }
    }
}

bool operator==(const Json& a, const Json& b) {
    // Numbers compare by value, so 1 == 1.0.
    if (a.is_number() && b.is_number() && a.type() != b.type()) {
        return a.as_double() == b.as_double();
    }
    return a.value_ == b.value_;
}

} // namespace http
