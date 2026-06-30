#pragma once
#include <string>
#include <string_view>

enum class LogLevel { DEBUG, INFO, WARN, ERR };

class Logger {
public:
    static void init(const char* tag, const char* log_file = nullptr);
    static void log(LogLevel level, std::string_view msg);
    static void debug(std::string_view msg) { log(LogLevel::DEBUG, msg); }
    static void info(std::string_view msg)  { log(LogLevel::INFO,  msg); }
    static void warn(std::string_view msg)  { log(LogLevel::WARN,  msg); }
    static void err(std::string_view msg)   { log(LogLevel::ERR,   msg); }

private:
    static const char* s_tag;
    static const char* s_file;
};
