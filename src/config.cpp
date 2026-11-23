#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <system_error>

namespace http {

namespace {

constexpr std::uint64_t kMaxThreads = 1024;
constexpr std::uint64_t kMaxTimeoutMs = 24ull * 60 * 60 * 1000; // one day

std::string_view trim(std::string_view text) noexcept {
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!text.empty() && is_space(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::string to_lower(std::string_view text) {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

// Parses an unsigned decimal in [min, max]; the whole string must be digits.
std::optional<std::uint64_t> parse_unsigned(std::string_view text, std::uint64_t min,
                                            std::uint64_t max) noexcept {
    std::uint64_t value = 0;
    const char* end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || ptr != end || value < min || value > max) {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> parse_bool(std::string_view text) {
    const std::string lower = to_lower(text);
    if (lower == "true" || lower == "yes" || lower == "on" || lower == "1") {
        return true;
    }
    if (lower == "false" || lower == "no" || lower == "off" || lower == "0") {
        return false;
    }
    return std::nullopt;
}

// Context for one "key = value" line, used to build error messages.
struct Entry {
    std::string_view source;
    std::size_t line;
    std::string_view key;
    std::string_view value;

    [[noreturn]] void fail(std::string_view what) const {
        std::ostringstream message;
        message << source << ':' << line << ": " << what;
        throw ConfigError(message.str());
    }

    [[noreturn]] void invalid(std::string_view expected) const {
        fail("invalid " + std::string(key) + " '" + std::string(value) + "' (expected " +
             std::string(expected) + ")");
    }

    std::uint64_t as_unsigned(std::uint64_t min, std::uint64_t max) const {
        if (const auto number = parse_unsigned(value, min, max)) {
            return *number;
        }
        invalid("a number from " + std::to_string(min) + " to " + std::to_string(max));
    }

    std::chrono::milliseconds as_millis() const {
        return std::chrono::milliseconds(as_unsigned(1, kMaxTimeoutMs));
    }

    bool as_bool() const {
        if (const auto flag = parse_bool(value)) {
            return *flag;
        }
        invalid("true or false");
    }

    std::string as_non_empty() const {
        if (value.empty()) {
            fail(std::string(key) + " must not be empty");
        }
        return std::string(value);
    }
};

using Setter = std::function<void(ServerConfig&, const Entry&)>;

const std::map<std::string, Setter, std::less<>>& setters() {
    static const std::map<std::string, Setter, std::less<>> table = {
        {"host", [](ServerConfig& c, const Entry& e) { c.host = e.as_non_empty(); }},
        {"port",
         [](ServerConfig& c, const Entry& e) {
             c.port = static_cast<std::uint16_t>(
                 e.as_unsigned(0, std::numeric_limits<std::uint16_t>::max()));
         }},
        {"document_root",
         [](ServerConfig& c, const Entry& e) { c.document_root = e.as_non_empty(); }},
        {"error_pages", [](ServerConfig& c, const Entry& e) { c.error_pages = e.as_non_empty(); }},
        {"threads",
         [](ServerConfig& c, const Entry& e) {
             c.threads = static_cast<std::size_t>(e.as_unsigned(0, kMaxThreads));
         }},
        {"client_timeout_ms",
         [](ServerConfig& c, const Entry& e) { c.client_timeout = e.as_millis(); }},
        {"keep_alive",
         [](ServerConfig& c, const Entry& e) { c.keep_alive.enabled = e.as_bool(); }},
        {"keep_alive_timeout_ms",
         [](ServerConfig& c, const Entry& e) { c.keep_alive.idle_timeout = e.as_millis(); }},
        {"keep_alive_max_requests",
         [](ServerConfig& c, const Entry& e) {
             c.keep_alive.max_requests = static_cast<std::size_t>(e.as_unsigned(1, 1'000'000));
         }},
        {"log_level",
         [](ServerConfig& c, const Entry& e) {
             if (const auto level = log_level_from_string(e.value)) {
                 c.log_level = *level;
             } else {
                 e.invalid("debug, info, warn, error or off");
             }
         }},
    };
    return table;
}

} // namespace

ServerConfig parse_config(std::string_view text, std::string_view source) {
    ServerConfig config;
    std::size_t line_number = 0;

    while (!text.empty()) {
        ++line_number;
        const std::size_t newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);

        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }

        Entry entry{source, line_number, {}, {}};
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            entry.fail("expected 'key = value', got '" + std::string(line) + "'");
        }
        entry.key = trim(line.substr(0, equals));
        entry.value = trim(line.substr(equals + 1));
        if (entry.key.empty()) {
            entry.fail("missing key before '='");
        }
        if (entry.value.size() >= 2 && entry.value.front() == '"' && entry.value.back() == '"') {
            entry.value = entry.value.substr(1, entry.value.size() - 2);
        }

        const auto& table = setters();
        const auto setter = table.find(entry.key);
        if (setter == table.end()) {
            entry.fail("unknown setting '" + std::string(entry.key) + "'");
        }
        setter->second(config, entry);
    }
    return config;
}

ServerConfig load_config(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw ConfigError("cannot open config file '" + path.string() + "'");
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) {
        throw ConfigError("cannot read config file '" + path.string() + "'");
    }
    return parse_config(contents.str(), path.string());
}

std::string to_config_string(const ServerConfig& config) {
    std::ostringstream out;
    out << "host = " << config.host << '\n'
        << "port = " << config.port << '\n'
        << "document_root = " << config.document_root.string() << '\n'
        << "error_pages = " << config.error_pages.string() << '\n'
        << "threads = " << config.threads << '\n'
        << "client_timeout_ms = " << config.client_timeout.count() << '\n'
        << "keep_alive = " << (config.keep_alive.enabled ? "true" : "false") << '\n'
        << "keep_alive_timeout_ms = " << config.keep_alive.idle_timeout.count() << '\n'
        << "keep_alive_max_requests = " << config.keep_alive.max_requests << '\n'
        << "log_level = " << to_lower(to_string(config.log_level)) << '\n';
    return out.str();
}

} // namespace http
