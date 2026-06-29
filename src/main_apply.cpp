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

// Paths — all inside module dir or /data/adb for persistence
static constexpr const char* LOG_FILE        = "/data/adb/yay/run.log";
static constexpr const char* RULES_JSON      = "/data/adb/yay/config/rules.json";
static constexpr const char* GAME_JSON       = "/data/adb/yay/config/game_config.json";
static constexpr const char* IO_JSON         = "/data/adb/yay/config/io_config.json";
static constexpr const char* RULES_HASH      = "/data/adb/yay/cache/rules.hash";
static constexpr const char* GAME_HASH       = "/data/adb/yay/cache/game.hash";

// ─── post-fs-data: resetprop + dex2oat flags ────────────────────────────────
// Runs before userspace fully up. Keeps it minimal and fast.
static void run_post(const char* moddir) {
    Logger::info("yay_apply: --post (post-fs-data)");

    // resetprop via Magisk's resetprop binary bundled in PATH by Magisk
    struct Prop { const char* key; const char* val; };
    static const Prop props[] = {
        {"dalvik.vm.systemuicompilerfilter",     "speed-profile"},
        {"dalvik.vm.systemservercompilerfilter", "speed-profile"},
        {"pm.dexopt.install",                    "speed-profile"},
        {"pm.dexopt.bg-dexopt",                  "speed-profile"},
        {"dalvik.vm.dex2oat-minidebuginfo",      "false"},
        {"dalvik.vm.minidebuginfo",              "false"},
        {"debug.hwui.skia_atrace_enabled",       "false"},
        {"debug.atrace.tags.enableflags",        "0"},
        {"persist.traced.enable",                "0"},
        {"persist.debug.trace",                  "0"},
        // Suppress phantom process killer UI — Android 12+
        {"persist.sys.fflag.override.settings_enable_monitor_phantom_procs", "false"},
    };

    for (auto& p : props) {
        auto r = exec_cmd({"resetprop", p.key, p.val}, 5);
        if (!r.ok())
            Logger::warn(std::string("post: resetprop failed: ") + p.key);
    }

    Logger::info("yay_apply: --post done");
}

// ─── boot: full subsystem run after boot_completed ──────────────────────────
static void run_boot() {
    Logger::info("yay_apply: --boot");

    // 1. I/O Scheduler
    auto io_cfg = load_json(IO_JSON);
    if (io_cfg) {
        apply_io_scheduler(*io_cfg);
    } else {
        // No config: apply sensible defaults inline
        Json defaults = {{"scheduler_preference", {"kyber", "mq-deadline", "deadline"}}};
        apply_io_scheduler(defaults);
    }

    // 2. Sysctl: TCP congestion control
    // Read available, prefer cubic (stable, well-tested on mobile)
    std::string avail = sysfs_read("/proc/sys/net/ipv4/tcp_available_congestion_control");
    if (avail.find("cubic") != std::string::npos)
        sysfs_write("/proc/sys/net/ipv4/tcp_congestion_control", "cubic");

    // 3. Rules (hash-guarded — skips if unchanged since last boot)
    auto rules_cfg = load_json(RULES_JSON);
    if (rules_cfg) {
        // Embed source path so rules_engine can hash the file directly
        (*rules_cfg)["_source_path"] = RULES_JSON;
        apply_rules(*rules_cfg, RULES_HASH);
    }

    // 4. Game mode (hash-guarded)
    auto game_cfg = load_json(GAME_JSON);
    if (game_cfg) {
        apply_game_mode(*game_cfg);
    }

    // 5. fstrim — deferred 60s to not compete with early boot I/O
    // Fork + sleep + fstrim: fire-and-forget, parent exits immediately
    pid_t pid = fork();
    if (pid == 0) {
        setsid(); // detach from parent session
        sleep(60);
        exec_cmd({"cmd", "sm", "fstrim"}, 120);
        _exit(0);
    }
    // Parent does not waitpid — intentional orphan after parent exits

    Logger::info("yay_apply: --boot done");
}

// ─── rules-only: triggered by inotify watcher on rules.json change ──────────
static void run_rules(bool force) {
    Logger::info(std::string("yay_apply: --rules") + (force ? " --force" : ""));
    auto cfg = load_json(RULES_JSON);
    if (!cfg) { Logger::err("yay_apply: cannot load " + std::string(RULES_JSON)); return; }
    (*cfg)["_source_path"] = RULES_JSON;
    apply_rules(*cfg, RULES_HASH, force);
}

// ─── game-only: triggered by inotify watcher on game_config.json change ─────
static void run_game() {
    Logger::info("yay_apply: --game");
    auto cfg = load_json(GAME_JSON);
    if (!cfg) { Logger::err("yay_apply: cannot load " + std::string(GAME_JSON)); return; }
    apply_game_mode(*cfg);
}

// ─── full install-time run ───────────────────────────────────────────────────
static void run_full() {
    Logger::info("yay_apply: --full (install/update)");
    run_boot(); // boot already covers everything; force rules
    run_rules(/*force=*/true);
}

// ─── entry point ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    Logger::init("yay", LOG_FILE);

    const char* mode    = "--boot";
    bool        force   = false;
    const char* moddir  = "/data/adb/modules/yay";

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--post")    == 0) mode = "--post";
        else if (strcmp(argv[i], "--boot")    == 0) mode = "--boot";
        else if (strcmp(argv[i], "--rules")   == 0) mode = "--rules";
        else if (strcmp(argv[i], "--game")    == 0) mode = "--game";
        else if (strcmp(argv[i], "--full")    == 0) mode = "--full";
        else if (strcmp(argv[i], "--force")   == 0) force = true;
        else if (strcmp(argv[i], "--moddir")  == 0 && i+1 < argc) moddir = argv[++i];
    }

    if      (strcmp(mode, "--post")  == 0) run_post(moddir);
    else if (strcmp(mode, "--boot")  == 0) run_boot();
    else if (strcmp(mode, "--rules") == 0) run_rules(force);
    else if (strcmp(mode, "--game")  == 0) run_game();
    else if (strcmp(mode, "--full")  == 0) run_full();

    return 0;
}
