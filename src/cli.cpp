#include "cli.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <sstream>
#include <system_error>

namespace http {

namespace {

constexpr std::uint64_t kMaxThreads = 1024;

enum class Option {
    Config,
    Host,
    Port,
    Root,
    ErrorPages,
    Threads,
    LogLevel,
    NoKeepAlive,
    PrintConfig,
    Help,
    Version,
};

struct OptionSpec {
    Option id;
    char short_name; // '\0' when there is none
    std::string_view long_name;
    std::string_view value_name; // empty for flags
    std::string_view description;
};

constexpr std::array<OptionSpec, 11> kOptions = {{
    {Option::Config, 'c', "config", "FILE", "read settings from FILE (default: ./server.conf)"},
    {Option::Host, 'H', "host", "ADDR", "address to bind to (default: 0.0.0.0)"},
    {Option::Port, 'p', "port", "PORT", "port to listen on, 0 = any free port (default: 8080)"},
    {Option::Root, 'r', "root", "DIR", "document root for static files (default: public)"},
    {Option::ErrorPages, 'e', "error-pages", "DIR", "directory with error page templates"},
    {Option::Threads, 't', "threads", "N", "worker threads, 0 = one per CPU core"},
    {Option::LogLevel, 'l', "log-level", "LEVEL", "debug, info, warn, error or off"},
    {Option::NoKeepAlive, '\0', "no-keep-alive", "", "close every connection after one response"},
    {Option::PrintConfig, '\0', "print-config", "", "print the effective configuration and exit"},
    {Option::Help, 'h', "help", "", "show this help and exit"},
    {Option::Version, 'V', "version", "", "show the version and exit"},
}};

const OptionSpec* find_long(std::string_view name) noexcept {
    for (const auto& spec : kOptions) {
        if (spec.long_name == name) {
            return &spec;
        }
    }
    return nullptr;
}

const OptionSpec* find_short(char name) noexcept {
    for (const auto& spec : kOptions) {
        if (spec.short_name != '\0' && spec.short_name == name) {
            return &spec;
        }
    }
    return nullptr;
}

std::string display_name(const OptionSpec& spec) {
    return "--" + std::string(spec.long_name);
}

[[noreturn]] void invalid_value(std::string_view option, std::string_view value,
                                std::string_view expected) {
    throw CliError("invalid value '" + std::string(value) + "' for " + std::string(option) +
                   " (expected " + std::string(expected) + ")");
}

std::uint64_t parse_number(std::string_view option, std::string_view value, std::uint64_t max) {
    std::uint64_t number = 0;
    const char* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(value.data(), end, number);
    if (value.empty() || error != std::errc{} || ptr != end || number > max) {
        invalid_value(option, value, "a number from 0 to " + std::to_string(max));
    }
    return number;
}

std::uint16_t parse_port(std::string_view option, std::string_view value) {
    return static_cast<std::uint16_t>(
        parse_number(option, value, std::numeric_limits<std::uint16_t>::max()));
}

std::string non_empty(std::string_view option, std::string_view value) {
    if (value.empty()) {
        throw CliError(std::string(option) + " must not be empty");
    }
    return std::string(value);
}

void set_option(CliOptions& options, const OptionSpec& spec, std::string_view value) {
    const std::string name = display_name(spec);
    switch (spec.id) {
    case Option::Config:
        options.config_file = non_empty(name, value);
        break;
    case Option::Host:
        options.host = non_empty(name, value);
        break;
    case Option::Port:
        options.port = parse_port(name, value);
        break;
    case Option::Root:
        options.document_root = non_empty(name, value);
        break;
    case Option::ErrorPages:
        options.error_pages = non_empty(name, value);
        break;
    case Option::Threads:
        options.threads = static_cast<std::size_t>(parse_number(name, value, kMaxThreads));
        break;
    case Option::LogLevel:
        if (const auto level = log_level_from_string(value)) {
            options.log_level = *level;
        } else {
            invalid_value(name, value, "debug, info, warn, error or off");
        }
        break;
    case Option::NoKeepAlive:
        options.keep_alive = false;
        break;
    case Option::PrintConfig:
        options.print_config = true;
        break;
    case Option::Help:
        options.show_help = true;
        break;
    case Option::Version:
        options.show_version = true;
        break;
    }
}

void set_positional(CliOptions& options, std::size_t index, std::string_view value) {
    switch (index) {
    case 0:
        if (options.port) {
            throw CliError("port given both as --port and as a positional argument");
        }
        options.port = parse_port("port argument", value);
        break;
    case 1:
        if (options.document_root) {
            throw CliError("document root given both as --root and as a positional argument");
        }
        options.document_root = non_empty("document root argument", value);
        break;
    default:
        throw CliError("unexpected argument '" + std::string(value) + "'");
    }
}

} // namespace

CliOptions parse_cli(int argc, const char* const* argv) {
    CliOptions options;
    std::size_t positional = 0;
    bool options_ended = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (options_ended || arg == "-" || arg.size() < 2 || arg.front() != '-') {
            set_positional(options, positional++, arg);
            continue;
        }
        if (arg == "--") {
            options_ended = true;
            continue;
        }

        const OptionSpec* spec = nullptr;
        std::optional<std::string_view> inline_value;

        if (arg.substr(0, 2) == "--") {
            std::string_view name = arg.substr(2);
            if (const std::size_t equals = name.find('='); equals != std::string_view::npos) {
                inline_value = name.substr(equals + 1);
                name = name.substr(0, equals);
            }
            spec = find_long(name);
            if (spec == nullptr) {
                throw CliError("unknown option '--" + std::string(name) + "'");
            }
        } else {
            spec = find_short(arg[1]);
            if (spec == nullptr) {
                throw CliError("unknown option '-" + std::string(1, arg[1]) + "'");
            }
            if (arg.size() > 2) {
                if (spec->value_name.empty()) {
                    throw CliError("option '-" + std::string(1, arg[1]) +
                                   "' does not take a value");
                }
                inline_value = arg.substr(2); // -p8080
            }
        }

        if (spec->value_name.empty()) {
            if (inline_value) {
                throw CliError("option " + display_name(*spec) + " does not take a value");
            }
            set_option(options, *spec, {});
            continue;
        }

        if (!inline_value) {
            if (i + 1 >= argc) {
                throw CliError("option " + display_name(*spec) + " requires a value (" +
                               std::string(spec->value_name) + ")");
            }
            inline_value = argv[++i];
        }
        set_option(options, *spec, *inline_value);
    }
    return options;
}

void apply_cli(const CliOptions& options, ServerConfig& config) {
    if (options.host) {
        config.host = *options.host;
    }
    if (options.port) {
        config.port = *options.port;
    }
    if (options.document_root) {
        config.document_root = *options.document_root;
    }
    if (options.error_pages) {
        config.error_pages = *options.error_pages;
    }
    if (options.threads) {
        config.threads = *options.threads;
    }
    if (options.log_level) {
        config.log_level = *options.log_level;
    }
    if (options.keep_alive) {
        config.keep_alive.enabled = *options.keep_alive;
    }
}

std::string cli_usage(std::string_view program) {
    std::ostringstream out;
    out << "Usage: " << program << " [options] [port] [document_root]\n\n"
        << "A small HTTP/1.1 server written from scratch in C++17.\n\n"
        << "Options:\n";

    for (const auto& spec : kOptions) {
        std::string left = "  ";
        left += spec.short_name != '\0' ? std::string("-") + spec.short_name + ", " : "    ";
        left += "--";
        left += spec.long_name;
        if (!spec.value_name.empty()) {
            left += ' ';
            left += spec.value_name;
        }
        constexpr std::size_t kColumn = 28;
        left.resize(std::max(left.size() + 1, kColumn), ' ');
        out << left << spec.description << '\n';
    }

    out << "\nSettings are applied in this order, later ones winning: built-in defaults,\n"
        << "the config file (--config, else $SERVER_CONFIG, else ./server.conf if present),\n"
        << "the LOG_LEVEL environment variable, then command-line options.\n";
    return out.str();
}

} // namespace http
