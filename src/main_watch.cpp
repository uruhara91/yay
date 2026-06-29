#include "logger.h"
#include "exec_util.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>

// Paths watched
static constexpr const char* RULES_JSON   = "/data/adb/yay/config/rules.json";
static constexpr const char* GAME_JSON    = "/data/adb/yay/config/game_config.json";
static constexpr const char* IO_JSON      = "/data/adb/yay/config/io_config.json";
static constexpr const char* CONFIG_DIR   = "/data/adb/yay/config";
static constexpr const char* LOG_FILE     = "/data/adb/yay/run.log";
static constexpr const char* YAY_APPLY    = "/data/adb/modules/yay/bin/yay_apply";

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }

// Debounce: after an inotify event, wait this long for writes to settle
static constexpr int DEBOUNCE_MS = 1500;

// Map inotify watch descriptor to which --mode to trigger
struct Watch {
    int         wd;
    const char* filename; // basename to match inside dir event
    const char* mode;     // --rules / --game / --io
};

int main(int argc, char* /*argv*/[]) {
    Logger::init("yay_watch", LOG_FILE);
    Logger::info("yay_watch: starting inotify watcher");

    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);

    int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (ifd < 0) {
        Logger::err("yay_watch: inotify_init1 failed: " + std::string(strerror(errno)));
        return 1;
    }

    // Watch the config directory for close_write events (file fully written).
    // Watching the dir rather than individual files survives atomic renames.
    int wd = inotify_add_watch(ifd, CONFIG_DIR, IN_CLOSE_WRITE | IN_MOVED_TO);
    if (wd < 0) {
        Logger::err("yay_watch: inotify_add_watch failed on " +
                    std::string(CONFIG_DIR) + ": " + strerror(errno));
        close(ifd);
        return 1;
    }

    // Map filenames to yay_apply modes
    struct FileMode { const char* name; const char* mode; };
    static const FileMode mappings[] = {
        {"rules.json",       "--rules"},
        {"game_config.json", "--game"},
        {"io_config.json",   "--boot"},  // io change: re-run full boot scheduler
    };

    // inotify event buffer — aligned for struct inotify_event
    alignas(struct inotify_event)
    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];

    struct pollfd pfd{ ifd, POLLIN, 0 };

    Logger::info("yay_watch: watching " + std::string(CONFIG_DIR));

    while (!g_stop) {
        int ret = poll(&pfd, 1, 5000); // 5s tick — check g_stop periodically
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue; // timeout, loop

        if (!(pfd.revents & POLLIN)) continue;

        ssize_t len = read(ifd, buf, sizeof(buf));
        if (len <= 0) continue;

        // Parse event(s) from buffer
        const char* ptr = buf;
        const char* triggered_mode = nullptr;

        while (ptr < buf + len) {
            auto* ev = reinterpret_cast<const struct inotify_event*>(ptr);
            ptr += sizeof(struct inotify_event) + ev->len;

            if (ev->len == 0) continue;
            std::string fname(ev->name);

            for (auto& m : mappings) {
                if (fname == m.name) {
                    triggered_mode = m.mode;
                    break;
                }
            }
            if (triggered_mode) break;
        }

        if (!triggered_mode) continue;

        // Debounce: drain any further events for DEBOUNCE_MS
        // (editor saves, atomic renames fire multiple events)
        Logger::debug("yay_watch: change detected, debouncing " +
                      std::to_string(DEBOUNCE_MS) + "ms...");
        bool more = true;
        while (more) {
            int d = poll(&pfd, 1, DEBOUNCE_MS);
            if (d <= 0) { more = false; break; }
            // Drain without processing — just discard
            read(ifd, buf, sizeof(buf));
        }

        // Spawn yay_apply with the targeted mode
        Logger::info(std::string("yay_watch: spawning yay_apply ") + triggered_mode);
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            execl(YAY_APPLY, YAY_APPLY, triggered_mode, nullptr);
            _exit(127);
        } else if (pid > 0) {
            // Brief wait — we care about completion, but don't block long
            int status = 0;
            waitpid(pid, &status, 0);
            int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            Logger::info("yay_watch: yay_apply " + std::string(triggered_mode) +
                         " exited=" + std::to_string(code));
        }
    }

    Logger::info("yay_watch: stopping");
    inotify_rm_watch(ifd, wd);
    close(ifd);
    return 0;
}
