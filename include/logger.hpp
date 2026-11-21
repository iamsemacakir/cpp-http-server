#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace http {

enum class LogLevel { Debug, Info, Warn, Error, Off };

std::string_view to_string(LogLevel level) noexcept;

// Parses "debug", "info", "warn"/"warning", "error", "off" (case-insensitive).
std::optional<LogLevel> log_level_from_string(std::string_view text) noexcept;

// Thread-safe logger writing one line per message:
//   2026-11-21 18:05:12.345 [INFO ] GET / -> 200
// Debug/Info go to the info stream (stdout), Warn/Error to the error stream
// (stderr). Every line is flushed so output is visible immediately.
class Logger {
public:
    // The process-wide logger used by the log_* helpers below.
    static Logger& instance();

    Logger(std::ostream& info_stream, std::ostream& error_stream) noexcept;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void set_level(LogLevel level) noexcept;
    LogLevel level() const noexcept;
    bool enabled(LogLevel level) const noexcept;

    void write(LogLevel level, std::string_view message);

    // Formats all arguments with operator<< (only if the level is enabled).
    template <typename... Args>
    void log(LogLevel level, Args&&... args) {
        if (!enabled(level)) {
            return;
        }
        std::ostringstream message;
        (message << ... << std::forward<Args>(args));
        write(level, message.str());
    }

private:
    std::ostream& info_stream_;
    std::ostream& error_stream_;
    std::atomic<LogLevel> level_{LogLevel::Info};
    std::mutex mutex_; // serialises writes so lines never interleave
};

template <typename... Args>
void log_debug(Args&&... args) {
    Logger::instance().log(LogLevel::Debug, std::forward<Args>(args)...);
}

template <typename... Args>
void log_info(Args&&... args) {
    Logger::instance().log(LogLevel::Info, std::forward<Args>(args)...);
}

template <typename... Args>
void log_warn(Args&&... args) {
    Logger::instance().log(LogLevel::Warn, std::forward<Args>(args)...);
}

template <typename... Args>
void log_error(Args&&... args) {
    Logger::instance().log(LogLevel::Error, std::forward<Args>(args)...);
}

} // namespace http
