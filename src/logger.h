#pragma once

#include <source_location>
#include <string>
#include <string_view>

enum class LogLevel { DEBUG, INFO, WARN, ERR };

class Logger {
public:
    static void init(
        std::string_view tag,
        std::string_view log_file = {}) noexcept;

    static void log(
        LogLevel level,
        std::string_view msg,
        const std::source_location& location =
            std::source_location::current()) noexcept;

    static void debug(
        std::string_view msg,
        const std::source_location& location =
            std::source_location::current()) noexcept
    {
        log(LogLevel::DEBUG, msg, location);
    }

    static void info(
        std::string_view msg,
        const std::source_location& location =
            std::source_location::current()) noexcept
    {
        log(LogLevel::INFO, msg, location);
    }

    static void warn(
        std::string_view msg,
        const std::source_location& location =
            std::source_location::current()) noexcept
    {
        log(LogLevel::WARN, msg, location);
    }

    static void err(
        std::string_view msg,
        const std::source_location& location =
            std::source_location::current()) noexcept
    {
        log(LogLevel::ERR, msg, location);
    }
};
