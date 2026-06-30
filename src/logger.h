#pragma once

#include <source_location>
#include <string_view>

enum class LogLevel { DEBUG, INFO, WARN, ERR };

class Logger {
public:
    /// Initialize logger with tag and optional file path.
    /// @param tag Android log tag (typically "yay")
    /// @param log_file Path to persistent log file. If nullptr, logs to Android log only.
    static void init(std::string_view tag, std::string_view log_file = "") noexcept;

    /// Log a message at the specified level.
    /// @param level Log level (DEBUG, INFO, WARN, ERR)
    /// @param msg Message to log
    /// @param loc Source location for debugging (default: caller's location)
    static void log(
        LogLevel level,
        std::string_view msg,
        const std::source_location& loc = std::source_location::current()
    ) noexcept;

    /// Convenience wrappers
    /// @{
    static void debug(std::string_view msg) noexcept {
        log(LogLevel::DEBUG, msg);
    }
    static void info(std::string_view msg) noexcept {
        log(LogLevel::INFO, msg);
    }
    static void warn(std::string_view msg) noexcept {
        log(LogLevel::WARN, msg);
    }
    static void err(std::string_view msg) noexcept {
        log(LogLevel::ERR, msg);
    }
    /// @}

private:
    static constexpr std::string_view s_empty_tag = "yay";
    static constexpr std::string_view s_empty_file = "";

    static std::string_view s_tag;
    static std::string_view s_file;
};
