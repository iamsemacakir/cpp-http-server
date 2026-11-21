#include "logger.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>

namespace http {

namespace {

// Local time with milliseconds, e.g. "2026-11-21 18:05:12.345".
std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t seconds = system_clock::to_time_t(now);
    const auto millis = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm local{};
    localtime_r(&seconds, &local);
    char buffer[32];
    const std::size_t length = std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);

    char result[48];
    std::snprintf(result, sizeof(result), "%.*s.%03d", static_cast<int>(length), buffer,
                  static_cast<int>(millis));
    return result;
}

} // namespace

std::string_view to_string(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Off: return "OFF";
    }
    return "UNKNOWN";
}

std::optional<LogLevel> log_level_from_string(std::string_view text) noexcept {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "debug") {
        return LogLevel::Debug;
    }
    if (lower == "info") {
        return LogLevel::Info;
    }
    if (lower == "warn" || lower == "warning") {
        return LogLevel::Warn;
    }
    if (lower == "error") {
        return LogLevel::Error;
    }
    if (lower == "off" || lower == "none") {
        return LogLevel::Off;
    }
    return std::nullopt;
}

Logger& Logger::instance() {
    static Logger logger(std::cout, std::cerr);
    return logger;
}

Logger::Logger(std::ostream& info_stream, std::ostream& error_stream) noexcept
    : info_stream_(info_stream), error_stream_(error_stream) {}

void Logger::set_level(LogLevel level) noexcept {
    level_.store(level, std::memory_order_relaxed);
}

LogLevel Logger::level() const noexcept {
    return level_.load(std::memory_order_relaxed);
}

bool Logger::enabled(LogLevel level) const noexcept {
    return level != LogLevel::Off && level >= this->level();
}

void Logger::write(LogLevel level, std::string_view message) {
    if (!enabled(level)) {
        return;
    }
    char label[8];
    std::snprintf(label, sizeof(label), "%-5.*s", static_cast<int>(to_string(level).size()),
                  to_string(level).data());

    const std::string line = timestamp() + " [" + label + "] " + std::string(message) + '\n';
    std::lock_guard lock(mutex_);
    std::ostream& out = level >= LogLevel::Warn ? error_stream_ : info_stream_;
    out << line << std::flush;
}

} // namespace http
