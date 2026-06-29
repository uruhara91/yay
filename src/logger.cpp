#include "logger.h"
#include <android/log.h>
#include <cstdio>
#include <ctime>

const char* Logger::s_tag  = "yay";
const char* Logger::s_file = nullptr;

void Logger::init(const char* tag, const char* log_file) {
    s_tag  = tag;
    s_file = log_file;
}

void Logger::log(LogLevel level, std::string_view msg) {
    android_LogPriority prio;
    const char* prefix;
    switch (level) {
        case LogLevel::DEBUG: prio = ANDROID_LOG_DEBUG; prefix = "D"; break;
        case LogLevel::INFO:  prio = ANDROID_LOG_INFO;  prefix = "I"; break;
        case LogLevel::WARN:  prio = ANDROID_LOG_WARN;  prefix = "W"; break;
        case LogLevel::ERR:   prio = ANDROID_LOG_ERROR; prefix = "E"; break;
    }
    __android_log_print(prio, s_tag, "%.*s", (int)msg.size(), msg.data());

    if (s_file) {
        FILE* f = fopen(s_file, "a");
        if (!f) return;
        time_t now = time(nullptr);
        char ts[20];
        strftime(ts, sizeof(ts), "%m-%d %H:%M:%S", localtime(&now));
        fprintf(f, "[%s %s] %.*s\n", ts, prefix, (int)msg.size(), msg.data());
        fclose(f);
    }
}
