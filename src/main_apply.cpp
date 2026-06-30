#include "exec_util.h"
#include "game_mode.h"
#include "io_scheduler.h"
#include "json_config.h"
#include "logger.h"
#include "rules_engine.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdio>

// ─── Data paths — all under /data/adb/yay/ ───────────────────────────────────
static constexpr const char* LOG_FILE    = "/data/adb/yay/run.log";
static constexpr const char* LOG_MAX     = "/data/adb/yay/run.log.1"; // rotation backup
static constexpr const char* RULES_JSON  = "/data/adb/yay/config/rules.json";
static constexpr const char* GAME_JSON   = "/data/adb/yay/config/game_config.json";
static constexpr const char* IO_JSON     = "/data/adb/yay/config/io_config.json";
static constexpr const char* RULES_HASH  = "/data/adb/yay/cache/rules.hash";
static constexpr const char* GAME_HASH   = "/data/adb/yay/cache/game.hash";

static constexpr long LOG_MAX_BYTES = 512 * 1024; // 512 KB

// Simple log rotation: rename run.log → run.log.1 when over limit.
// Keeps at most 2 files total — no unbounded growth.
static void rotate_log_if_needed() {
    struct stat st;
    if (stat(LOG_FILE, &st) != 0) return;
    if (st.st_size < LOG_MAX_BYTES) return;
    rename(LOG_FILE, LOG_MAX); // overwrite previous backup
}

// ─── post-fs-data ─────────────────────────────────────────────────────────────
// Runs before userspace is up. Minimal, fast — only resetprop.
static void run_post() {
    Logger::info("yay: --post");

    struct Prop { const char* key; const char* val; };
    static const Prop props[] = {
        // Dex2oat: speed-profile gives fast startup with background JIT
        {"dalvik.vm.systemuicompilerfilter",     "speed-profile"},
        {"dalvik.vm.systemservercompilerfilter", "speed-profile"},
        {"pm.dexopt.install",                    "speed-profile"},
        {"pm.dexopt.bg-dexopt",                  "speed-profile"},
        // Disable mini debug info — saves RAM, slightly smaller odex
        {"dalvik.vm.dex2oat-minidebuginfo",      "false"},
        {"dalvik.vm.minidebuginfo",              "false"},
        // Disable tracing overhead
        {"debug.hwui.skia_atrace_enabled",       "false"},
        {"debug.atrace.tags.enableflags",        "0"},
        {"persist.traced.enable",                "0"},
        {"persist.debug.trace",                  "0"},
        // Phantom process killer: suppress noisy UI (Android 12+)
        {"persist.sys.fflag.override.settings_enable_monitor_phantom_procs", "false"},
    };

    for (auto& p : props) {
        auto r = exec_cmd({"resetprop", p.key, p.val}, 5);
        if (!r.ok())
            Logger::warn(std::string("post: resetprop failed: ") + p.key);
    }

    Logger::info("yay: --post done");
}

// ─── io ───────────────────────────────────────────────────────────────────────
// Always runs at boot — I/O scheduler state resets on every reboot.
static void run_io() {
    auto cfg = load_json(IO_JSON);
    if (cfg) {
        apply_io_scheduler(*cfg);
    } else {
        Json defaults = {{"scheduler_preference", {"kyber", "mq-deadline", "deadline"}}};
        apply_io_scheduler(defaults);
    }

    // TCP congestion control: cubic is well-tested on mobile
    std::string avail = sysfs_read("/proc/sys/net/ipv4/tcp_available_congestion_control");
    if (avail.find("cubic") != std::string::npos)
        sysfs_write("/proc/sys/net/ipv4/tcp_congestion_control", "cubic");
}

// ─── rules ────────────────────────────────────────────────────────────────────
static void run_rules(bool force) {
    Logger::info(std::string("yay: --rules") + (force ? " --force" : ""));
    auto cfg = load_json(RULES_JSON);
    if (!cfg) {
        Logger::err("yay: cannot load rules.json");
        return;
    }
    (*cfg)["_source_path"] = RULES_JSON;
    apply_rules(*cfg, RULES_HASH, force);
}

// ─── game ─────────────────────────────────────────────────────────────────────
static void run_game() {
    Logger::info("yay: --game");
    auto cfg = load_json(GAME_JSON);
    if (!cfg) {
        Logger::err("yay: cannot load game_config.json");
        return;
    }
    apply_game_mode(*cfg);
}

// ─── boot ─────────────────────────────────────────────────────────────────────
// Called from service.sh after boot_completed. Runs io always, rules+game
// only if config changed since last boot (hash-guarded inside each subsystem).
static void run_boot() {
    Logger::info("yay: --boot");

    run_io();
    run_rules(/*force=*/false);
    run_game();

    // fstrim: deferred 60s — avoids competing with early-boot I/O.
    // Orphaned child: parent exits immediately, child runs in background.
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        sleep(60);
        exec_cmd({"cmd", "sm", "fstrim"}, 120);
        _exit(0);
    }
    // Parent does NOT waitpid — intentional.

    Logger::info("yay: --boot done");
}

// ─── full (install / update) ──────────────────────────────────────────────────
// Force-apply everything. Does NOT call run_boot() to avoid duplicate io run.
static void run_full() {
    Logger::info("yay: --full");

    run_io();
    run_rules(/*force=*/true);   // force: ignore hash, always apply on install
    run_game();

    Logger::info("yay: --full done");
}

// ─── entry point ──────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    rotate_log_if_needed();
    Logger::init("yay", LOG_FILE);

    const char* mode  = "--boot";
    bool        force = false;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--post")  == 0) mode  = "--post";
        else if (strcmp(argv[i], "--boot")  == 0) mode  = "--boot";
        else if (strcmp(argv[i], "--rules") == 0) mode  = "--rules";
        else if (strcmp(argv[i], "--game")  == 0) mode  = "--game";
        else if (strcmp(argv[i], "--full")  == 0) mode  = "--full";
        else if (strcmp(argv[i], "--io")    == 0) mode  = "--io";
        else if (strcmp(argv[i], "--force") == 0) force = true;
    }

    if      (strcmp(mode, "--post")  == 0) run_post();
    else if (strcmp(mode, "--boot")  == 0) run_boot();
    else if (strcmp(mode, "--rules") == 0) run_rules(force);
    else if (strcmp(mode, "--game")  == 0) run_game();
    else if (strcmp(mode, "--full")  == 0) run_full();
    else if (strcmp(mode, "--io")    == 0) run_io();

    return 0;
}