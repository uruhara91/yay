#include "exec_util.h"

#include "logger.h"

#include <algorithm>
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

// Non-blocking waitpid: returns true if the child has already exited
// (exit_code populated), false if it's still running. Does not block.
[[nodiscard]]
bool try_reap(pid_t pid, int& exit_code) noexcept
{
    int status = 0;
    const pid_t ret = ::waitpid(pid, &status, WNOHANG);
    if (ret == 0) {
        return false; // still running
    }
    if (ret < 0) {
        // ECHILD etc — treat as gone so we don't spin forever on it.
        exit_code = -1;
        return true;
    }

    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return true;
}

// One in-flight child spawned for exec_batch.
struct BatchChild {
    pid_t pid = -1;
    FileDescriptor read_fd;
    std::chrono::steady_clock::time_point deadline {};
    size_t result_index = 0;
    bool started = false; // false until actually forked (budget may skip it)
};

// Fork+exec a single command, non-blocking — caller owns the returned fd.
// Returns pid < 0 on failure (fork/pipe2 error); caller should treat the
// corresponding result as a failure without killing the whole batch.
[[nodiscard]]
pid_t spawn_one(
    const std::vector<std::string>& argv_owned,
    FileDescriptor& out_read_fd) noexcept
{
    if (argv_owned.empty()) {
        return -1;
    }

    std::array<int, 2U> pipefd {-1, -1};
    if (::pipe2(pipefd.data(), O_CLOEXEC) < 0) {
        Logger::err("exec_util: pipe2 failed (batch)");
        return -1;
    }

    FileDescriptor read_fd(pipefd[0]);
    FileDescriptor write_fd(pipefd[1]);

    // execvp mutates neither argv contents nor the owning strings here, but
    // make_argv() needs a non-const vector<string>& to fill char* pointers
    // into existing storage — copy once per spawn, owned by the child setup.
    std::vector<std::string> owned_copy = argv_owned;
    auto raw_args = make_argv(owned_copy);

    const pid_t pid = ::fork();
    if (pid < 0) {
        Logger::err("exec_util: fork failed (batch)");
        return -1;
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
    out_read_fd = std::move(read_fd);
    return pid;
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

std::vector<ExecResult> exec_batch(
    std::span<const std::vector<std::string>> commands,
    size_t max_concurrent,
    std::chrono::seconds item_timeout,
    std::chrono::seconds total_budget,
    size_t max_consecutive_spawn_failures) noexcept
{
    std::vector<ExecResult> results(commands.size());
    if (commands.empty()) {
        return results;
    }

    try {
        const auto batch_deadline =
            std::chrono::steady_clock::now() + total_budget;
        const size_t concurrency =
            std::max<size_t>(1U, std::min(max_concurrent, commands.size()));
        const size_t spawn_failure_limit =
            std::max<size_t>(1U, max_consecutive_spawn_failures);

        std::vector<BatchChild> in_flight;
        in_flight.reserve(concurrency);

        size_t next_to_start = 0U;
        bool budget_exceeded = false;
        size_t consecutive_spawn_failures = 0U;
        bool spawn_circuit_open = false;

        // Launch the initial wave up to `concurrency`.
        //
        // spawn_circuit_open guards against a burst of fork()/pipe2()
        // failures in a row — almost always a sign the system is out of
        // file descriptors or process slots, not a transient blip. When
        // that happens, retrying immediately in a tight loop just burns
        // CPU while the underlying resource pressure is still present, so
        // once the threshold is hit we stop attempting further spawns for
        // the rest of this batch and fail the remaining commands outright.
        // A single isolated failure does not trip this — the counter
        // resets on the next successful spawn — only a genuine run of
        // failures does.
        auto try_start_next = [&]() noexcept {
            while (in_flight.size() < concurrency &&
                   next_to_start < commands.size()) {
                if (std::chrono::steady_clock::now() >= batch_deadline) {
                    budget_exceeded = true;
                    return;
                }

                if (spawn_circuit_open) {
                    results[next_to_start] = ExecResult{-1, {}};
                    ++next_to_start;
                    continue;
                }

                BatchChild child;
                child.result_index = next_to_start;
                child.pid = spawn_one(commands[next_to_start], child.read_fd);
                child.deadline =
                    std::chrono::steady_clock::now() + item_timeout;

                if (child.pid < 0) {
                    // Spawn failed outright — record failure now, don't
                    // occupy a concurrency slot for it.
                    results[next_to_start] = ExecResult{-1, {}};
                    ++next_to_start;
                    ++consecutive_spawn_failures;
                    if (consecutive_spawn_failures >= spawn_failure_limit) {
                        Logger::err(
                            "exec_util: " +
                            std::to_string(consecutive_spawn_failures) +
                            " consecutive spawn failures — aborting "
                            "remaining spawns in this batch");
                        spawn_circuit_open = true;
                    }
                    continue;
                }

                consecutive_spawn_failures = 0U;
                child.started = true;
                in_flight.push_back(std::move(child));
                ++next_to_start;
            }
        };

        try_start_next();

        while (!in_flight.empty()) {
            if (std::chrono::steady_clock::now() >= batch_deadline) {
                budget_exceeded = true;
                break;
            }

            std::vector<struct pollfd> pfds;
            pfds.reserve(in_flight.size());
            for (auto& child : in_flight) {
                pfds.push_back(
                    {child.read_fd.get(), POLLIN, 0});
            }

            const int ret = ::poll(
                pfds.data(),
                static_cast<nfds_t>(pfds.size()),
                static_cast<int>(kPollInterval.count()));

            if (ret < 0 && errno != EINTR) {
                Logger::err("exec_util: batch poll error");
                break;
            }

            std::array<char, kReadBufferSize> buffer {};

            // Drain any fds that are ready.
            if (ret > 0) {
                for (size_t i = 0U; i < in_flight.size(); ++i) {
                    const auto& revents = pfds[i].revents;
                    if ((revents & (POLLIN | POLLHUP)) == 0 &&
                        (revents & (POLLERR | POLLNVAL)) == 0) {
                        continue;
                    }

                    auto& child = in_flight[i];
                    if ((revents & POLLIN) != 0) {
                        const auto n = ::read(
                            child.read_fd.get(),
                            buffer.data(),
                            buffer.size());
                        if (n > 0) {
                            results[child.result_index].stdout_str.append(
                                buffer.data(),
                                static_cast<size_t>(n));
                        }
                    }
                }
            }

            // Reap anything that has exited or blown its per-item deadline,
            // independent of poll readiness — a child can exit with no
            // further output (POLLHUP without POLLIN already handled above,
            // but WNOHANG here is the authoritative exit check).
            const auto now = std::chrono::steady_clock::now();
            std::vector<BatchChild> still_running;
            still_running.reserve(in_flight.size());

            for (auto& child : in_flight) {
                int exit_code = -1;
                if (try_reap(child.pid, exit_code)) {
                    // Drain any final buffered bytes before closing.
                    while (true) {
                        const auto n = ::read(
                            child.read_fd.get(),
                            buffer.data(),
                            buffer.size());
                        if (n <= 0) {
                            break;
                        }
                        results[child.result_index].stdout_str.append(
                            buffer.data(),
                            static_cast<size_t>(n));
                    }
                    results[child.result_index].exit_code = exit_code;
                    continue; // child.read_fd closes via destructor below
                }

                if (now >= child.deadline) {
                    Logger::warn("exec_util: batch item timeout, killing pid");
                    static_cast<void>(::kill(child.pid, SIGKILL));
                    static_cast<void>(wait_for_child(child.pid));
                    results[child.result_index] = ExecResult{-1, {}};
                    continue;
                }

                still_running.push_back(std::move(child));
            }

            in_flight = std::move(still_running);

            try_start_next();
            if (budget_exceeded) {
                break;
            }
        }

        if (budget_exceeded) {
            Logger::err(
                "exec_util: batch total budget exceeded — killing " +
                std::to_string(in_flight.size()) +
                " in-flight, skipping " +
                std::to_string(commands.size() - next_to_start) +
                " unstarted");

            for (auto& child : in_flight) {
                static_cast<void>(::kill(child.pid, SIGKILL));
                static_cast<void>(wait_for_child(child.pid));
                results[child.result_index] = ExecResult{-1, {}};
            }
            for (size_t i = next_to_start; i < commands.size(); ++i) {
                results[i] = ExecResult{-1, {}};
            }
        }

        return results;
    } catch (...) {
        Logger::err("exec_util: exception in batch — returning partial results");
        return results;
    }
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
