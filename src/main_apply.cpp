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

// ─── Data paths — all under /data/adb/yay/ ───────────────────────────────────
static constexpr const char* LOG_FILE    = "/data/adb/yay/run.log";
static constexpr const char* LOG_PREV    = "/data/adb/yay/run.log.1";
static constexpr const char* RULES_JSON  = "/data/adb/yay/config/rules.json";
static constexpr const char* GAME_JSON   = "/data/adb/yay/config/game_config.json";
static constexpr const char* IO_JSON     = "/data/adb/yay/config/io_config.json";
// Only rules.json has a hash cache. Component disable and appops overrides
// are persistent Android-side state (survive reboot on their own), so a
// hash guard avoids redundant binder calls when nothing changed. Game mode
// (downscale, log cleanup) and the I/O scheduler are runtime state Android
// resets every boot — they always re-apply unconditionally, no hash
// involved, see apply_game_mode() in game_mode.cpp and run_io() below.
static constexpr const char* RULES_HASH  = "/data/adb/yay/cache/rules.hash";

// ─── helpers ─────────────────────────────────────────────────────────────────

// Rotate log on boot: move current log to .1, start fresh.
// Called before Logger::init so the new session starts on a clean file.
// Using rename() here is safe because Logger hasn't opened the fd yet.
static void rotate_log_on_boot() noexcept {
    ::rename(LOG_FILE, LOG_PREV); // best-effort, ignore error
}

// Load a config file and reject it outright if it doesn't have the expected
// shape for `kind` — see validate_config_shape() in json_config.h/.cpp for
// what "shape" means here. This runs for every mode (--rules, --game,
// --boot, --full, dry-run included): a structurally wrong config should
// never reach the apply layer at all, since apply_components/apply_appops/
// apply_game_mode are only designed to skip malformed *individual entries*,
// not recover from e.g. "components" being an object instead of an array.
// Most valuable once these files may be written by a web UI rather than
// hand-edited — a bug on that side should surface here as a clear rejected
// config, not as a partially-applied or confusingly-empty run.
[[nodiscard]]
static std::optional<Json> load_and_validate(const char* path, ConfigKind kind) {
    auto cfg = load_json(path);
    if (!cfg) {
        Logger::err(std::string("yay: cannot load ") + path);
        return std::nullopt;
    }

    std::string error;
    if (!validate_config_shape(*cfg, kind, &error)) {
        Logger::err(std::string("yay: ") + path + " failed shape validation: " + error);
        return std::nullopt;
    }

    return cfg;
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
        // HWC Virtual Display Support: lets SurfaceFlinger back virtual
        // displays (screen recording, casting/WFD) with the hardware
        // composer instead of falling back to GLES compositing. Read once
        // by SurfaceFlinger at its own init, so this must land before it
        // starts — hence --post, not --boot.
        {"debug.sf.enable_hwc_vds",              "1"},
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
    auto cfg = load_and_validate(IO_JSON, ConfigKind::IoConfig);
    if (cfg) {
        apply_io_scheduler(*cfg);
    } else {
        Json defaults = {{"scheduler_preference", {"kyber", "mq-deadline", "deadline"}}};
        apply_io_scheduler(defaults);
    }

    // TCP congestion control: cubic is well-tested on mobile
    std::string avail = sysfs_read("/proc/sys/net/ipv4/tcp_available_congestion_control");
    if (avail.find("cubic") != std::string::npos)
        static_cast<void>(
            sysfs_write("/proc/sys/net/ipv4/tcp_congestion_control", "cubic"));
}

// ─── rules ────────────────────────────────────────────────────────────────────
static void run_rules(bool force, bool dry_run) {
    Logger::info(std::string("yay: --rules") +
                 (force ? " --force" : "") +
                 (dry_run ? " --dry-run" : ""));
    auto cfg = load_and_validate(RULES_JSON, ConfigKind::Rules);
    if (!cfg) {
        return;
    }
    (*cfg)["_source_path"] = RULES_JSON;
    static_cast<void>(apply_rules(*cfg, RULES_HASH, force, dry_run));
}

// ─── game ─────────────────────────────────────────────────────────────────────
static void run_game() {
    Logger::info("yay: --game");
    auto cfg = load_and_validate(GAME_JSON, ConfigKind::GameConfig);
    if (!cfg) {
        return;
    }
    apply_game_mode(*cfg);
}

// ─── boot ─────────────────────────────────────────────────────────────────────
// Called from service.sh after boot_completed. Runs io and game mode
// unconditionally every boot (Android resets both kinds of state on its
// own), and rules only if rules.json changed since last run (hash-guarded
// inside apply_rules — see rules_engine.cpp).
static void run_boot() {
    Logger::info("yay: --boot");

    run_io();
    run_rules(/*force=*/false, /*dry_run=*/false);
    run_game();

    // fstrim: deferred 60s — avoids competing with early-boot I/O.
    // Orphaned child: parent exits immediately, child runs in background.
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        sleep(60);
        static_cast<void>(exec_cmd({"cmd", "sm", "fstrim"}, 120));
        _exit(0);
    }
    // Parent does NOT waitpid — intentional.

    Logger::info("yay: --boot done");
}

// ─── full (install / update) ──────────────────────────────────────────────────
// Force-apply everything.
static void run_full() {
    Logger::info("yay: --full");

    run_io();
    run_rules(/*force=*/true, /*dry_run=*/false);
    run_game();

    Logger::info("yay: --full done");
}

// ─── entry point ──────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    const char* mode    = "--boot";
    bool        force   = false;
    bool        dry_run = false;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--post")    == 0) mode    = "--post";
        else if (strcmp(argv[i], "--boot")    == 0) mode    = "--boot";
        else if (strcmp(argv[i], "--rules")   == 0) mode    = "--rules";
        else if (strcmp(argv[i], "--game")    == 0) mode    = "--game";
        else if (strcmp(argv[i], "--full")    == 0) mode    = "--full";
        else if (strcmp(argv[i], "--io")      == 0) mode    = "--io";
        else if (strcmp(argv[i], "--force")   == 0) force   = true;
        else if (strcmp(argv[i], "--dry-run") == 0) dry_run = true;
    }

    // Rotate log before opening it so each boot starts on a clean file.
    // Only --boot and --full represent a new device session; --rules/--game/
    // --io are in-session triggered runs that should append to the current log.
    if (strcmp(mode, "--boot") == 0 || strcmp(mode, "--full") == 0) {
        rotate_log_on_boot();
    }

    Logger::init("yay", LOG_FILE);

    // --dry-run only makes sense paired with --rules (it previews `cmd
    // package`/`cmd appops` calls without invoking them or touching the
    // hash cache — see apply_rules()). It's silently ignored for every
    // other mode rather than treated as an error, since a caller scripting
    // this (e.g. a future web UI "preview changes" button) is more likely
    // to always pass the flag than to special-case which mode supports it.
    if (dry_run && strcmp(mode, "--rules") != 0) {
        Logger::warn("yay: --dry-run only applies to --rules, ignoring for this mode");
        dry_run = false;
    }

    if      (strcmp(mode, "--post")  == 0) run_post();
    else if (strcmp(mode, "--boot")  == 0) run_boot();
    else if (strcmp(mode, "--rules") == 0) run_rules(force, dry_run);
    else if (strcmp(mode, "--game")  == 0) run_game();
    else if (strcmp(mode, "--full")  == 0) run_full();
    else if (strcmp(mode, "--io")    == 0) run_io();

    return 0;
}
