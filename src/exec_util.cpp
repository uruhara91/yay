#include "exec_util.h"

#include "logger.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr std::chrono::milliseconds kPollInterval{200};
constexpr size_t kReadBufferSize = 512U;
constexpr size_t kSysfsReadBufferSize = 256U;

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

[[nodiscard]]
std::vector<std::string> own_args(std::span<const std::string_view> argv)
{
    std::vector<std::string> owned;
    owned.reserve(argv.size());

    for (const auto arg : argv) {
        owned.emplace_back(arg);
    }

    return owned;
}

[[nodiscard]]
std::vector<char*> make_argv(std::vector<std::string>& argv)
{
    std::vector<char*> raw;
    raw.reserve(argv.size() + 1U);

    for (auto& arg : argv) {
        raw.push_back(arg.data());
    }
    raw.push_back(nullptr);

    return raw;
}

[[nodiscard]]
int wait_for_child(pid_t pid) noexcept
{
    int status = 0;

    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return -1;
}

} // namespace

ExecResult exec_cmd(
    std::span<const std::string_view> argv,
    std::chrono::seconds timeout) noexcept
{
    ExecResult result;
    if (argv.empty()) {
        return result;
    }

    try {
        auto owned_args = own_args(argv);
        auto raw_args = make_argv(owned_args);

        std::array<int, 2U> pipefd {-1, -1};
        if (::pipe2(pipefd.data(), O_CLOEXEC) < 0) {
            Logger::err("exec_util: pipe2 failed");
            return result;
        }

        FileDescriptor read_fd(pipefd[0]);
        FileDescriptor write_fd(pipefd[1]);

        const pid_t pid = ::fork();
        if (pid < 0) {
            Logger::err("exec_util: fork failed");
            return result;
        }

        if (pid == 0) {
            static_cast<void>(::dup2(write_fd.get(), STDOUT_FILENO));
            static_cast<void>(::dup2(write_fd.get(), STDERR_FILENO));
            read_fd.reset();
            write_fd.reset();
            ::execvp(raw_args[0], raw_args.data());
            _exit(127);
        }

        write_fd.reset();

        std::array<char, kReadBufferSize> buffer {};
        std::chrono::milliseconds elapsed{0};
        const auto timeout_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
        struct pollfd pfd {
            read_fd.get(),
            POLLIN,
            0
        };
        bool killed = false;

        while (true) {
            const int ret = ::poll(
                &pfd,
                1,
                static_cast<int>(kPollInterval.count()));

            if (ret > 0) {
                if ((pfd.revents & (POLLIN | POLLHUP)) != 0) {
                    const auto n =
                        ::read(read_fd.get(), buffer.data(), buffer.size());
                    if (n > 0) {
                        result.stdout_str.append(
                            buffer.data(),
                            static_cast<size_t>(n));
                    } else if (n == 0) {
                        break;
                    } else if (errno != EINTR) {
                        break;
                    }
                }

                if ((pfd.revents & (POLLERR | POLLNVAL)) != 0) {
                    break;
                }
            } else if (ret == 0) {
                elapsed += kPollInterval;
                if (timeout.count() > 0 && elapsed >= timeout_ms) {
                    Logger::warn("exec_util: timeout, killing child");
                    static_cast<void>(::kill(pid, SIGKILL));
                    killed = true;
                    break;
                }
            } else if (errno != EINTR) {
                break;
            }
        }

        read_fd.reset();

        result.exit_code = killed ? -1 : wait_for_child(pid);
        if (killed) {
            static_cast<void>(wait_for_child(pid));
        }

        return result;
    } catch (...) {
        return result;
    }
}

ExecResult exec_cmd(
    std::initializer_list<std::string_view> argv,
    int timeout_sec) noexcept
{
    return exec_cmd(
        std::span<const std::string_view>(argv.begin(), argv.size()),
        std::chrono::seconds(timeout_sec));
}

bool sysfs_write(std::string_view path, std::string_view value) noexcept
{
    try {
        const std::string owned_path(path);
        FileDescriptor fd(::open(owned_path.c_str(), O_WRONLY | O_CLOEXEC));
        if (!fd.valid()) {
            return false;
        }

        return write_all(fd.get(), value);
    } catch (...) {
        return false;
    }
}

std::string sysfs_read(std::string_view path) noexcept
{
    try {
        const std::string owned_path(path);
        FileDescriptor fd(::open(owned_path.c_str(), O_RDONLY | O_CLOEXEC));
        if (!fd.valid()) {
            return {};
        }

        std::array<char, kSysfsReadBufferSize> buffer {};
        ssize_t n = 0;
        do {
            n = ::read(fd.get(), buffer.data(), buffer.size() - 1U);
        } while (n < 0 && errno == EINTR);

        if (n <= 0) {
            return {};
        }

        size_t size = static_cast<size_t>(n);
        while (size > 0U &&
               (buffer[size - 1U] == '\n' || buffer[size - 1U] == '\r')) {
            --size;
        }

        return std::string(buffer.data(), size);
    } catch (...) {
        return {};
    }
}
