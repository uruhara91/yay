#include "logger.h"
#include "exec_util.h"

#include <array>
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

constexpr size_t kModeCount = sizeof(MAPPINGS) / sizeof(MAPPINGS[0]);

static void spawn_apply(const char* mode) {
    Logger::info(std::string("yay_watch: spawning yay_apply ") + mode);

    pid_t pid = fork();
    if (pid < 0) {
        Logger::err("yay_watch: fork failed");
        return;
    }
    if (pid == 0) {
        setsid();
        // Close stdin and redirect stdout/stderr to /dev/null so the
        // child does not inherit the watcher's log fd. yay_apply opens
        // its own log via Logger::init — inheriting our fd would cause
        // interleaved, non-atomic writes to run.log from two processes.
        int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
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

// Scan one inotify read buffer for events matching a known config filename.
// Marks every match in `triggered` (does NOT stop at the first one) — a
// single editor save batch, or a sync tool touching multiple files close
// together, can legitimately produce events for more than one of
// rules.json / game_config.json / io_config.json in the same read().
// Returns true if at least one new mode was newly marked.
static bool scan_events(const char* buf, ssize_t len, std::array<bool, kModeCount>& triggered) {
    bool any_new = false;
    const char* ptr = buf;
    while (ptr < buf + len) {
        auto* ev = reinterpret_cast<const struct inotify_event*>(ptr);
        ptr += sizeof(struct inotify_event) + ev->len;
        if (ev->len == 0) continue;

        for (size_t i = 0; i < kModeCount; ++i) {
            if (strcmp(ev->name, MAPPINGS[i].name) == 0) {
                if (!triggered[i]) any_new = true;
                triggered[i] = true;
            }
        }
    }
    return any_new;
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

    // Buffer for inotify events. A single read() can return multiple events;
    // the kernel coalesces bursts. One event slot (sizeof header + NAME_MAX)
    // is only enough for a single event — with two files saved in quick
    // succession the second event would silently be dropped from that read().
    // 64 slots is far more than we will ever see in one burst but still tiny
    // in absolute terms (~4 KB on most systems).
    alignas(struct inotify_event)
    char buf[64 * (sizeof(struct inotify_event) + NAME_MAX + 1)];

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

        std::array<bool, kModeCount> triggered{};
        scan_events(buf, len, triggered);

        bool any_triggered = false;
        for (bool t : triggered) any_triggered |= t;
        if (!any_triggered) continue;

        // Debounce: drain further events until quiet for DEBOUNCE_MS.
        // Handles editors that write multiple events per save, and keeps
        // scanning the drained events too — a file that changes again
        // during the debounce window, or a second config file that only
        // changes a moment later (e.g. a sync tool touching files one at
        // a time), still gets picked up instead of silently missed.
        Logger::debug("yay_watch: change detected, debouncing...");
        while (true) {
            int d = poll(&pfd, 1, DEBOUNCE_MS);
            if (d <= 0) break;
            const auto drained = read(ifd, buf, sizeof(buf));
            if (drained > 0) {
                scan_events(buf, drained, triggered);
            }
        }

        for (size_t i = 0; i < kModeCount; ++i) {
            if (triggered[i]) {
                spawn_apply(MAPPINGS[i].mode);
            }
        }
    }

    Logger::info("yay_watch: stopping");
    inotify_rm_watch(ifd, wd);
    close(ifd);
    return 0;
}
