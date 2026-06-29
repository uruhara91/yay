#include "exec_util.h"
#include "logger.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

ExecResult exec_cmd(const std::vector<std::string>& argv, int timeout_sec) {
    ExecResult result{-1, ""};
    if (argv.empty()) return result;

    // Build null-terminated argv for execvp
    std::vector<const char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto& s : argv) cargv.push_back(s.c_str());
    cargv.push_back(nullptr);

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
        Logger::err("exec_util: pipe2 failed: " + std::string(strerror(errno)));
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        Logger::err("exec_util: fork failed: " + std::string(strerror(errno)));
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }

    if (pid == 0) {
        // Child: redirect stdout+stderr to pipe
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(cargv[0], const_cast<char* const*>(cargv.data()));
        _exit(127); // execvp failed
    }

    // Parent: read from pipe until child exits
    close(pipefd[1]);

    char buf[512];
    ssize_t n;
    int elapsed_ms = 0;
    const int timeout_ms = timeout_sec * 1000;

    struct pollfd pfd{pipefd[0], POLLIN, 0};
    while (true) {
        int ret = poll(&pfd, 1, 200);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            n = read(pipefd[0], buf, sizeof(buf));
            if (n > 0) result.stdout_str.append(buf, n);
            else break; // EOF
        } else if (ret == 0) {
            elapsed_ms += 200;
            if (timeout_sec > 0 && elapsed_ms >= timeout_ms) {
                Logger::warn("exec_util: timeout, killing pid " + std::to_string(pid));
                kill(pid, SIGKILL);
                break;
            }
        } else {
            break; // poll error or POLLHUP
        }
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

bool sysfs_write(const char* path, const char* value) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t len = static_cast<ssize_t>(strlen(value));
    ssize_t written = write(fd, value, len);
    close(fd);
    return written == len;
}

std::string sysfs_read(const char* path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return "";
    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return "";
    // Trim trailing newline
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) n--;
    buf[n] = '\0';
    return std::string(buf);
}
