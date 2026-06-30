#include "logger.h"

#include <android/log.h>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace {

/// RAII wrapper for file descriptor
class FileDescriptor {
public:
    explicit FileDescriptor(int fd = -1) noexcept : m_fd(fd) {}

    ~FileDescriptor() noexcept {
        if (m_fd >= 0) {
            close(m_fd);
        }
    }

    // Non-copyable, movable
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : m_fd(other.release()) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        reset(other.release());
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return m_fd; }
    [[nodiscard]] bool valid() const noexcept { return m_fd >= 0; }

    int release() noexcept {
        int fd = m_fd;
        m_fd = -1;
        return fd;
    }

    void reset(int fd = -1) noexcept {
        if (m_fd >= 0) {
            close(m_fd);
        }
        m_fd = fd;
    }

private:
    int m_fd;
};

/// Format current time as "MM-DD HH:MM:SS"
/// Uses thread-safe localtime_r
[[nodiscard]] std::string format_timestamp() noexcept {
    char buf[20] = {};
    time_t now = time(nullptr);
    struct tm tm_buf = {};

    if (localtime_r(&now, &tm_buf) == nullptr) {
        return "[??-?? ??:??:??]";
    }

    strftime(buf, sizeof(buf), "[%m-%d %H:%M:%S]", &tm_buf);
    return buf;
}

/// Get log level prefix character
[[nodiscard]] constexpr const char* level_prefix(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::DEBUG:
            return "D";
        case LogLevel::INFO:
            return "I";
        case LogLevel::WARN:
            return "W";
        case LogLevel::ERR:
            return "E";
    }
    return "?";
}

/// Get Android log priority
[[nodiscard]] constexpr android_LogPriority level_to_priority(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::DEBUG:
            return ANDROID_LOG_DEBUG;
        case LogLevel::INFO:
            return ANDROID_LOG_INFO;
        case LogLevel::WARN:
            return ANDROID_LOG_WARN;
        case LogLevel::ERR:
            return ANDROID_LOG_ERROR;
    }
    return ANDROID_LOG_DEFAULT;
}

/// Atomically write to file with rotate-on-overflow
/// Max log file size: 512 KB
constexpr long LOG_MAX_BYTES = 512 * 1024;

void atomic_log_write(
    std::string_view file_path,
    std::string_view timestamp,
    std::string_view prefix,
    std::string_view msg) noexcept
{
    if (file_path.empty()) {
        return;
    }

    // Check if rotation needed before write
    struct stat st = {};
    if (stat(file_path.data(), &st) == 0 && st.st_size >= LOG_MAX_BYTES) {
        std::string backup(file_path);
        backup += ".1";
        rename(file_path.data(), backup.c_str());
    }

    // Append to log file
    FileDescriptor fd(open(file_path.data(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640));

    if (!fd.valid()) {
        return;
    }

    // Build log entry: "[timestamp] [prefix] message\n"
    // Use write() directly to avoid string allocations
    const std::string entry = std::string(timestamp) + " [" + std::string(prefix) + "] " +
                              std::string(msg) + "\n";

    // Loop until all bytes written (sysfs-style robustness)
    const char* ptr = entry.c_str();
    ssize_t remaining = static_cast<ssize_t>(entry.size());

    while (remaining > 0) {
        ssize_t written = write(fd.get(), ptr, remaining);
        if (written <= 0) {
            break;
        }
        ptr += written;
        remaining -= written;
    }
}

} // namespace

std::string_view Logger::s_tag = "yay";
std::string_view Logger::s_file = "";

void Logger::init(std::string_view tag, std::string_view log_file) noexcept {
    s_tag = tag.empty() ? s_empty_tag : tag;
    s_file = log_file.empty() ? s_empty_file : log_file;
}

void Logger::log(
    LogLevel level,
    std::string_view msg,
    const std::source_location& loc) noexcept
{
    // Log to Android log
    android_LogPriority prio = level_to_priority(level);
    __android_log_print(
        prio,
        s_tag.data(),
        "%.*s",
        static_cast<int>(msg.size()),
        msg.data());

    // Log to file if configured
    if (!s_file.empty()) {
        std::string timestamp = format_timestamp();
        const char* prefix = level_prefix(level);
        atomic_log_write(s_file, timestamp, prefix, msg);
    }
}
