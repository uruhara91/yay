#include "logger.h"

#if __has_include(<android/log.h>)
#include <android/log.h>
#define YAY_HAS_ANDROID_LOG 1
#else
#define YAY_HAS_ANDROID_LOG 0
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr std::string_view kDefaultTag = "yay";
constexpr size_t kMaxLogBytes = 256U * 1024U;
constexpr mode_t kLogFileMode = 0600;

class FileDescriptor {
public:
    explicit FileDescriptor(int fd = -1) noexcept
        : fd_(fd)
    {
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept
        : fd_(other.release())
    {
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    ~FileDescriptor() noexcept
    {
        reset();
    }

    [[nodiscard]]
    int get() const noexcept
    {
        return fd_;
    }

    [[nodiscard]]
    bool valid() const noexcept
    {
        return fd_ >= 0;
    }

    [[nodiscard]]
    int release() noexcept
    {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

    void reset(int fd = -1) noexcept
    {
        if (fd_ >= 0) {
            while (::close(fd_) < 0 && errno == EINTR) {
            }
        }
        fd_ = fd;
    }

private:
    int fd_;
};

struct LoggerState {
    std::mutex mutex;
    std::string tag{kDefaultTag};
    std::string path;
    FileDescriptor file;
};

[[nodiscard]]
LoggerState& state() noexcept
{
    static LoggerState logger_state;
    return logger_state;
}

[[nodiscard]]
constexpr std::string_view level_prefix(LogLevel level) noexcept
{
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

#if YAY_HAS_ANDROID_LOG
[[nodiscard]]
constexpr android_LogPriority android_priority(LogLevel level) noexcept
{
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
#endif

[[nodiscard]]
std::string_view basename(std::string_view path) noexcept
{
    const auto pos = path.find_last_of("/\\");
    if (pos == std::string_view::npos) {
        return path;
    }
    return path.substr(pos + 1U);
}

[[nodiscard]]
bool write_all(int fd, std::string_view value) noexcept
{
    const auto* data = value.data();
    size_t remaining = value.size();

    while (remaining > 0U) {
        const auto written = ::write(fd, data, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            return false;
        }

        const auto written_size = static_cast<size_t>(written);
        data += written_size;
        remaining -= written_size;
    }

    return true;
}

void rotate_if_needed(LoggerState& log_state)
{
    if (log_state.path.empty()) {
        return;
    }

    struct stat st {};
    if (::stat(log_state.path.c_str(), &st) != 0) {
        return;
    }

    if (st.st_size >= 0 &&
        static_cast<unsigned long long>(st.st_size) <
            static_cast<unsigned long long>(kMaxLogBytes)) {
        return;
    }

    log_state.file.reset();

    const std::string rotated = log_state.path + ".1";
    static_cast<void>(::rename(log_state.path.c_str(), rotated.c_str()));
}

void open_log_file(LoggerState& log_state)
{
    if (log_state.path.empty()) {
        log_state.file.reset();
        return;
    }

    rotate_if_needed(log_state);

    log_state.file.reset(::open(
        log_state.path.c_str(),
        O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
        kLogFileMode));
}

void write_file_log(
    LoggerState& log_state,
    LogLevel level,
    std::string_view msg,
    const std::source_location& location)
{
    if (!log_state.file.valid()) {
        return;
    }

    rotate_if_needed(log_state);
    if (!log_state.file.valid()) {
        open_log_file(log_state);
    }
    if (!log_state.file.valid()) {
        return;
    }

    const std::time_t now = std::time(nullptr);
    std::tm local {};
    std::array<char, 20U> timestamp {};

    if (::localtime_r(&now, &local) == nullptr ||
        std::strftime(
            timestamp.data(),
            timestamp.size(),
            "%m-%d %H:%M:%S",
            &local) == 0U) {
        timestamp[0] = '\0';
    }

    std::array<char, 32U> line {};
    const int line_length = std::snprintf(
        line.data(),
        line.size(),
        "%u",
        location.line());

    const std::string_view file_name = basename(location.file_name());
    const size_t line_size =
        line_length > 0
            ? std::min(static_cast<size_t>(line_length), line.size() - 1U)
            : 0U;
    const std::string_view source_line(
        line.data(),
        line_size);

    static_cast<void>(write_all(log_state.file.get(), "["));
    static_cast<void>(write_all(log_state.file.get(), timestamp.data()));
    static_cast<void>(write_all(log_state.file.get(), " "));
    static_cast<void>(write_all(log_state.file.get(), level_prefix(level)));
    static_cast<void>(write_all(log_state.file.get(), " "));
    static_cast<void>(write_all(log_state.file.get(), file_name));
    static_cast<void>(write_all(log_state.file.get(), ":"));
    static_cast<void>(write_all(log_state.file.get(), source_line));
    static_cast<void>(write_all(log_state.file.get(), "] "));
    static_cast<void>(write_all(log_state.file.get(), msg));
    static_cast<void>(write_all(log_state.file.get(), "\n"));
}

} // namespace

void Logger::init(std::string_view tag, std::string_view log_file) noexcept
{
    try {
        auto& log_state = state();
        std::lock_guard lock(log_state.mutex);

        log_state.tag = tag.empty() ? std::string(kDefaultTag) : std::string(tag);
        log_state.path = std::string(log_file);
        open_log_file(log_state);
    } catch (...) {
    }
}

void Logger::log(
    LogLevel level,
    std::string_view msg,
    const std::source_location& location) noexcept
{
    try {
        auto& log_state = state();
        std::lock_guard lock(log_state.mutex);

#if YAY_HAS_ANDROID_LOG
        const size_t capped_size = std::min(
            msg.size(),
            static_cast<size_t>(std::numeric_limits<int>::max()));
        __android_log_print(
            android_priority(level),
            log_state.tag.c_str(),
            "%.*s",
            static_cast<int>(capped_size),
            msg.data());
#endif

        write_file_log(log_state, level, msg, location);
    } catch (...) {
    }
}
