#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "connection.hpp"
#include "logger.hpp"

namespace http {

// Thrown for unreadable config files and invalid entries. The message
// names the source and line, e.g. "server.conf:4: invalid port 'abc'".
class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Runtime settings of the server. Every field has a default, so a config
// file only needs to list the values it wants to change.
struct ServerConfig {
    std::string host = "0.0.0.0";
    std::uint16_t port = 8080;
    std::filesystem::path document_root = "public";
    std::filesystem::path error_pages = "errors";
    // Worker threads; 0 means one per hardware thread.
    std::size_t threads = 0;
    std::chrono::milliseconds client_timeout{10'000};
    KeepAliveOptions keep_alive{};
    LogLevel log_level = LogLevel::Info;
};

// Parses "key = value" lines on top of the defaults. Blank lines and lines
// starting with '#' or ';' are ignored, values may be wrapped in double
// quotes, and a repeated key overrides the earlier one. Unknown keys and
// invalid values throw ConfigError; `source` is used in its messages.
ServerConfig parse_config(std::string_view text, std::string_view source = "<config>");

// Reads and parses a config file. Throws ConfigError if it can't be read.
ServerConfig load_config(const std::filesystem::path& path);

// Renders a config back in the file format (used for the startup summary
// at debug level and handy for generating a template).
std::string to_config_string(const ServerConfig& config);

} // namespace http
