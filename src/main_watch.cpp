#include "logger.h"
#include "exec_util.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>

static constexpr const char* CONFIG_DIR = "/data/adb/yay/config";
static constexpr const char* LOG_FILE   = "/data/adb/yay/run.log";
static constexpr const char* YAY_APPLY  = "/data/adb/modules/yay/bin/yay_apply";
static constexpr int         DEBOUNCE_MS = 1500;

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }

struct FileMode {
    const char* name;
    const char* mode;
};

static const FileMode MAPPINGS[] = {
    {"rules.json",       "--rules"},
    {"game_config.json", "--game"},
    {"io_config.json",   "--io"},
};

static void spawn_apply(const char* mode) {
    Logger::info(std::string("yay_watch: spawning yay_apply ") + mode);

    pid_t pid = fork();
    if (pid < 0) {
        Logger::err("yay_watch: fork failed");
        return;
    }
    if (pid == 0) {
        setsid();
        execl(YAY_APPLY, YAY_APPLY, mode, nullptr);
        _exit(127);
    }

    // Wait for completion — watcher is already async from main flow
    int status = 0;
    waitpid(pid, &status, 0);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    Logger::info(std::string("yay_watch: yay_apply ") + mode +
                 " exit=" + std::to_string(code));
}

int main(int /*argc*/, char* /*argv*/[]) {
    Logger::init("yay_watch", LOG_FILE);
    Logger::info("yay_watch: starting");

    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);

    int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (ifd < 0) {
        Logger::err("yay_watch: inotify_init1 failed");
        return 1;
    }

    // Watch the config directory — survives atomic editor saves (rename-on-write)
    int wd = inotify_add_watch(ifd, CONFIG_DIR, IN_CLOSE_WRITE | IN_MOVED_TO);
    if (wd < 0) {
        Logger::err(std::string("yay_watch: cannot watch ") + CONFIG_DIR);
        close(ifd);
        return 1;
    }

    alignas(struct inotify_event)
    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];

    struct pollfd pfd{ifd, POLLIN, 0};

    Logger::info(std::string("yay_watch: watching ") + CONFIG_DIR);

    while (!g_stop) {
        // 5s tick — keeps g_stop check responsive without busy-wait
        int ret = poll(&pfd, 1, 5000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            Logger::err("yay_watch: poll error");
            break;
        }
        if (ret == 0) continue;
        if (!(pfd.revents & POLLIN)) continue;

        ssize_t len = read(ifd, buf, sizeof(buf));
        if (len <= 0) continue;

        // Scan events for a known filename
        const char* triggered_mode = nullptr;
        const char* ptr = buf;
        while (ptr < buf + len) {
            auto* ev = reinterpret_cast<const struct inotify_event*>(ptr);
            ptr += sizeof(struct inotify_event) + ev->len;
            if (ev->len == 0) continue;

            for (auto& m : MAPPINGS) {
                if (strcmp(ev->name, m.name) == 0) {
                    triggered_mode = m.mode;
                    break;
                }
            }
            if (triggered_mode) break;
        }

        if (!triggered_mode) continue;

        // Debounce: drain further events until quiet for DEBOUNCE_MS
        // Handles editors that write multiple events per save
        Logger::debug("yay_watch: change detected, debouncing...");
        while (true) {
            int d = poll(&pfd, 1, DEBOUNCE_MS);
            if (d <= 0) break;
            const auto drained = read(ifd, buf, sizeof(buf)); // drain, discard
            static_cast<void>(drained);
        }

        spawn_apply(triggered_mode);
    }

    Logger::info("yay_watch: stopping");
    inotify_rm_watch(ifd, wd);
    close(ifd);
    return 0;
}
