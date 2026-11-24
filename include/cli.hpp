#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "config.hpp"
#include "logger.hpp"

namespace http {

// Thrown for unknown options, missing values and invalid values. The
// message names the offending option, e.g. "invalid value '70000' for
// --port (expected a number from 0 to 65535)".
class CliError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Everything that can be given on the command line. Settings are optional
// so that only the ones the user actually passed override the config file.
struct CliOptions {
    std::optional<std::filesystem::path> config_file;
    std::optional<std::string> host;
    std::optional<std::uint16_t> port;
    std::optional<std::filesystem::path> document_root;
    std::optional<std::filesystem::path> error_pages;
    std::optional<std::size_t> threads;
    std::optional<LogLevel> log_level;
    std::optional<bool> keep_alive;

    bool show_help = false;
    bool show_version = false;
    bool print_config = false;
};

// Parses argv (argv[0] is skipped). Accepted forms:
//   --port 8080   --port=8080   -p 8080   -p8080
// "--" ends option parsing. For backwards compatibility up to two
// positional arguments are accepted as [port] [document_root]; an option
// and a positional argument for the same setting is an error.
CliOptions parse_cli(int argc, const char* const* argv);

// Overrides the fields of `config` that were set on the command line.
void apply_cli(const CliOptions& options, ServerConfig& config);

// The --help text, with `program` as the executable name.
std::string cli_usage(std::string_view program);

} // namespace http
