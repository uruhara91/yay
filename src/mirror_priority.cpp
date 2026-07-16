#include "mirror_priority.h"
#include "logger.h"
#include "renice_util.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/system_properties.h>
#include <thread>
#include <unistd.h>

namespace mirror_priority {

namespace {

constexpr const char* kAdbdPropertyName = "init.svc.adbd";
constexpr const char* kAdbdRunningValue = "running";

// Nice value for both adbd and scrcpy-server. Deliberately a little above
// (less negative than) the -10 Android's own ActivityManager already
// assigns the foreground game — see project notes: the goal is to keep
// these two off the tail of the run queue (so mirroring doesn't stutter
// under contention from unrelated background work), not to make them
// compete for the *same* top scheduling slot as the game itself. A tie at
// identical nice values would leave the kernel's CFS scheduler to break
// ties by vruntime alone, which is a source of avoidable jitter exactly
// when a frame is due to be captured.
constexpr int kNiceValue = -8;

// Best-effort I/O class, high-but-not-maximum priority within it. Realtime
// class is deliberately avoided — it can starve unrelated I/O (including,
// ultimately, the game's own asset/save I/O) if left running, which is a
// worse outcome than the jitter this whole feature exists to avoid.
constexpr renice::IoClass kIoClass = renice::IoClass::BestEffort;
constexpr int kIoLevel = 1; // 0 = highest, 7 = lowest within BestEffort

// How long to keep scanning /proc for scrcpy-server after adbd comes up,
// and how often. This is the one place this feature *does* wake the CPU
// periodically — but only for this short bounded window per adb session,
// never while idle. 20s at 500ms covers qtscrcpy's push+launch handshake
// with room to spare without polling indefinitely if the person just did
// a plain `adb shell` with no mirroring involved.
constexpr std::chrono::milliseconds kScrcpyPollInterval{500};
constexpr std::chrono::seconds kScrcpyPollBudget{20};

// procfs entries (comm, cmdline) are tiny — well under a typical page —
// so a single bounded read is sufficient; no need to loop for short reads
// the way a general-purpose file reader would.
constexpr size_t kProcReadBufSize = 4096;

[[nodiscard]]
bool read_proc_file(const std::string& path, std::string* out) noexcept {
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;

    char buf[kProcReadBufSize];
    ssize_t n = ::read(fd, buf, sizeof(buf));
    ::close(fd);

    if (n < 0) return false;
    out->assign(buf, static_cast<size_t>(n));
    return true;
}

[[nodiscard]]
bool read_first_line(const std::string& path, std::string* out) noexcept {
    std::string content;
    if (!read_proc_file(path, &content)) return false;
    auto nl = content.find('\n');
    *out = (nl == std::string::npos) ? content : content.substr(0, nl);
    return true;
}

// Matches the scrcpy/QtScrcpy server process. Verified against scrcpy's
// actual invocation (see Genymobile/scrcpy issue #5191 and the official
// dev docs): the launch command is
//   CLASSPATH=/data/local/tmp/scrcpy-server.jar app_process / com.genymobile.scrcpy.Server ...
// CLASSPATH is set as an *environment variable*, not a positional argv
// entry — so /proc/[pid]/cmdline never actually contains the literal
// string "scrcpy-server" at all for the Server process itself. The one
// thing guaranteed to appear in its argv is the Java main class name,
// since app_process takes it as a plain positional argument.
//
// Deliberately narrow: only matches when argv0 is app_process/
// app_process64 AND the exact main class "com.genymobile.scrcpy.Server"
// appears as a standalone argv token (space-bounded on both sides, since
// read_cmdline_full joins argv with spaces). Earlier versions of this
// matcher accepted the class-name substring anywhere in cmdline, which
// turned out to false-match processes actually observed on-device:
//   - "com.genymobile.scrcpy.CleanUp" (a separate helper process scrcpy
//     spawns — rejected by the exact-token check on its own)
//   - "/system/bin/sh -c su -c 'CLASSPATH=... app_process / com.genymobile.scrcpy.Server ...'"
//     and the non-su variant — the *wrapper shell* that launches Server,
//     whose single quoted argument to -c happens to contain the class
//     name as a token too. The argv0 check is what rejects these: their
//     argv0 is "sh", not "app_process"/"app_process64".
// Since the /proc scan stops at the first match found (and /proc's
// iteration order isn't guaranteed), the looser matcher could end up
// reniceing the wrapper shell or CleanUp instead of the actual Server
// process — exactly the process meant to be targeted was sometimes
// skipped entirely.
[[nodiscard]]
bool cmdline_matches_scrcpy_server(const std::string& cmdline) noexcept {
    if (cmdline.rfind("app_process", 0) != 0) return false; // argv0 check

    constexpr std::string_view kTarget = "com.genymobile.scrcpy.Server";
    size_t pos = cmdline.find(kTarget);
    if (pos == std::string::npos) return false;

    // Token-boundary check: must be preceded and followed by a space (or
    // string start/end) — rejects any hypothetical longer identifier that
    // merely contains this as a substring.
    bool left_ok = (pos == 0) || (cmdline[pos - 1] == ' ');
    size_t end = pos + kTarget.size();
    bool right_ok = (end == cmdline.size()) || (cmdline[end] == ' ');
    return left_ok && right_ok;
}

// Reads the full cmdline (with NULs replaced by spaces) for matching via
// cmdline_matches_scrcpy_server(). Matching anywhere in the full argv list
// (not just argv0, which is just "app_process"/"app_process64" for every
// app_process-launched process) is what actually distinguishes a scrcpy
// server launch from any other.
[[nodiscard]]
bool read_cmdline_full(const std::string& pid_dir, std::string* out) noexcept {
    std::string content;
    if (!read_proc_file(pid_dir + "/cmdline", &content)) return false;
    for (char& c : content) {
        if (c == '\0') c = ' ';
    }
    *out = std::move(content);
    return true;
}

[[nodiscard]]
bool is_all_digits(const std::string& s) noexcept {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

void log_renice_result(const char* label, pid_t pid, const renice::RenicePidResult& r) noexcept {
    if (r.ok()) {
        Logger::debug(std::string("mirror_priority: reniced ") + label +
                      " (pid=" + std::to_string(pid) + ") nice=" + std::to_string(kNiceValue));
        return;
    }
    if (!r.nice_ok) {
        Logger::debug(std::string("mirror_priority: setpriority failed for ") + label +
                      " (pid=" + std::to_string(pid) + ", errno=" + std::to_string(r.nice_errno) + ")");
    }
    if (!r.ioprio_ok) {
        Logger::debug(std::string("mirror_priority: ioprio_set failed for ") + label +
                      " (pid=" + std::to_string(pid) + ", errno=" + std::to_string(r.ioprio_errno) + ")");
    }
}

// Finds adbd's own pid by scanning /proc — init.svc.adbd tells us *that*
// it's running, not its pid. adbd is a stable, well-known process name
// (unlike scrcpy-server's cmdline shape, which varies by mirroring tool),
// so a comm-based match is sufficient and cheap: this only runs once per
// adb session (right after the property transitions to "running"), not
// in a loop.
[[nodiscard]]
pid_t find_adbd_pid() noexcept {
    DIR* proc = opendir("/proc");
    if (!proc) return -1;

    pid_t found = -1;
    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        std::string name(entry->d_name);
        if (!is_all_digits(name)) continue;

        std::string comm;
        if (!read_first_line("/proc/" + name + "/comm", &comm)) continue;
        // comm is newline-terminated by the kernel; getline already
        // stripped it, but trailing whitespace defensively just in case.
        while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r')) comm.pop_back();

        if (comm == "adbd") {
            try {
                found = static_cast<pid_t>(std::stoi(name));
            } catch (...) {
                continue;
            }
            break;
        }
    }
    closedir(proc);
    return found;
}

// Single pass over /proc looking for the scrcpy/QtScrcpy server process
// (see cmdline_matches_scrcpy_server for what's actually matched and why).
// Returns -1 if not found this pass (caller loops within the bounded
// polling window).
[[nodiscard]]
pid_t find_scrcpy_server_pid() noexcept {
    DIR* proc = opendir("/proc");
    if (!proc) return -1;

    pid_t found = -1;
    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        std::string name(entry->d_name);
        if (!is_all_digits(name)) continue;

        std::string cmdline;
        if (!read_cmdline_full("/proc/" + name, &cmdline)) continue;
        if (!cmdline_matches_scrcpy_server(cmdline)) continue;

        try {
            found = static_cast<pid_t>(std::stoi(name));
        } catch (...) {
            continue;
        }
        break;
    }
    closedir(proc);
    return found;
}

// Reads the current string value of a system property via the
// serial/callback-based API (the only reliable way to read a value once
// __system_property_wait has told us it changed — the older
// __system_property_get is deprecated and racier). Used at every point
// this file needs to inspect init.svc.adbd's current value.
[[nodiscard]]
std::string read_property_value(const prop_info* pi) noexcept {
    std::string value;
    __system_property_read_callback(
        pi,
        [](void* cookie, const char* /*name*/, const char* val, uint32_t /*serial*/) {
            *static_cast<std::string*>(cookie) = val;
        },
        &value);
    return value;
}

void renice_adbd_and_scan_for_scrcpy() noexcept {
    pid_t adbd_pid = find_adbd_pid();
    if (adbd_pid > 0) {
        auto r = renice::renice_pid(adbd_pid, kNiceValue, kIoClass, kIoLevel);
        log_renice_result("adbd", adbd_pid, r);
    } else {
        Logger::debug("mirror_priority: init.svc.adbd=running but adbd pid not found in /proc");
    }

    // Bounded scan for scrcpy-server. Stops early the moment it's found
    // and reniced — this is not a persistent poll, just a short grace
    // window to catch a process that (unlike adbd) has no init-managed
    // lifecycle property to wait on instead.
    const auto deadline = std::chrono::steady_clock::now() + kScrcpyPollBudget;
    bool found_scrcpy = false;
    while (std::chrono::steady_clock::now() < deadline) {
        pid_t scrcpy_pid = find_scrcpy_server_pid();
        if (scrcpy_pid > 0) {
            auto r = renice::renice_pid(scrcpy_pid, kNiceValue, kIoClass, kIoLevel);
            log_renice_result("scrcpy-server", scrcpy_pid, r);
            found_scrcpy = true;
            break;
        }
        std::this_thread::sleep_for(kScrcpyPollInterval);
    }
    if (!found_scrcpy) {
        Logger::debug("mirror_priority: scrcpy-server not observed within poll window (plain adb session, or mirroring not started)");
    }
}

} // namespace

void run() noexcept {
    Logger::debug("mirror_priority: watcher starting");

    const prop_info* pi = __system_property_find(kAdbdPropertyName);
    uint32_t serial = 0;

    // init.svc.adbd may not exist yet on some very early boots (init
    // hasn't parsed the adbd service definition yet) — if so, fall back
    // to waiting on the *global* property serial (pi = nullptr is valid
    // per the API and means "wake on any property change") until it
    // shows up, rather than busy-spinning trying to (re-)find it.
    while (pi == nullptr) {
        bool changed = __system_property_wait(nullptr, serial, &serial, nullptr);
        if (!changed) continue; // no timeout was given, so this shouldn't happen, but be defensive
        pi = __system_property_find(kAdbdPropertyName);
    }

    // Reset serial to 0 ("unknown — wake on any change") before entering
    // the real wait loop below. We deliberately don't use
    // __system_property_serial(pi) to prime this to the *exact* current
    // serial — that function isn't consistently declared across NDK
    // versions (confirmed absent from NDK 26.3.11579264's
    // sys/system_properties.h despite being documented as available since
    // API 26). Serial 0 works just as well here: the first
    // __system_property_wait call below returns essentially immediately
    // (any real property's serial is > 0), so this costs at most one
    // extra wait/read cycle at startup — negligible, and avoids depending
    // on a symbol not guaranteed present in every toolchain.
    serial = 0;

    // If adbd is already running by the time we get here (e.g. yay_watch
    // was started/restarted while a session was already active), handle
    // it immediately instead of waiting for the *next* transition.
    std::string current_value = read_property_value(pi);

    if (current_value == kAdbdRunningValue) {
        Logger::debug("mirror_priority: adbd already running at watcher startup");
        renice_adbd_and_scan_for_scrcpy();
    }

    for (;;) {
        // Blocks here — genuinely asleep, no CPU/wakeups — until
        // init.svc.adbd's value changes. This is the only wait in this
        // function that runs unconditionally forever; everything else is
        // bounded.
        bool changed = __system_property_wait(pi, serial, &serial, nullptr);
        if (!changed) continue;

        std::string value = read_property_value(pi);

        if (value == kAdbdRunningValue) {
            Logger::debug("mirror_priority: adbd started");
            renice_adbd_and_scan_for_scrcpy();
        }
        // Any other transition (stopping/stopped/restarting) needs no
        // action — scrcpy-server's own lifecycle is tied to the adb
        // session anyway, so it exits on its own when the connection
        // drops. Nothing to renice or clean up on our end.
    }
}

} // namespace mirror_priority