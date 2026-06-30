#include "rules_engine.h"
#include "exec_util.h"
#include "hash_util.h"
#include "logger.h"
#include <string>
#include <string_view>
#include <vector>

// Hash cache I/O delegated entirely to hashutil — atomic write (fsync + rename)
// instead of a local fopen/fprintf helper, so a power-cycle mid-write can't
// leave a corrupt/truncated hash file that would falsely skip a future apply.

// "cmd package disable pkg/comp1 pkg/comp2 ..." is NOT supported by Android
// pm — runDisable()/runEnable() in PackageManagerShellCommand take a single
// PACKAGE_OR_COMPONENT positional argument, no batch form. One component is
// still one process, but instead of running them strictly one-at-a-time we
// fire them through exec_batch with bounded concurrency: `cmd package` is
// IPC/IO-bound (binder call into system_server), so overlapping several at
// once is a real wall-clock win, not just busywork. Both the per-item
// timeout and the overall batch budget come from exec_batch's safe
// defaults (see exec_util.h) — they exist as a dead-man's switch for a
// wedged device, not because this path is expected to run often: it's only
// reached on install (--full) or when rules.json actually changes
// (hash-guarded below), never on a no-op boot.
static void apply_components(const Json& cfg, RulesResult& res) {
    if (!cfg.contains("components") || !cfg["components"].is_array()) return;

    struct Entry {
        std::string target;     // "pkg/component", for logging
        std::string cmd_action; // "enable" or "disable"
    };

    std::vector<Entry> entries;
    std::vector<std::vector<std::string>> commands;

    for (auto& entry : cfg["components"]) {
        if (!entry.value("enabled", true)) continue;

        std::string pkg  = entry.value("package",   "");
        std::string comp = entry.value("component",  "");
        std::string act  = entry.value("action",    "disable");

        if (pkg.empty() || comp.empty()) continue;

        std::string cmd_action = (act == "enable") ? "enable" : "disable";
        entries.push_back({pkg + "/" + comp, cmd_action});
        commands.push_back({"cmd", "package", cmd_action, pkg + "/" + comp});
    }

    if (commands.empty()) return;

    auto results = exec_batch(commands);

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        const auto& e = entries[i];

        // Android returns exit 0 and prints "new state: disabled" or similar
        // even if already disabled — treat both as success
        bool ok = r.ok() || r.stdout_str.find("new state") != std::string::npos;
        if (ok) {
            res.components_ok++;
        } else {
            Logger::warn("rules: " + e.cmd_action + " failed: " + e.target);
            res.failed++;
        }
    }
}

// Same batching rationale as apply_components — `cmd appops set` is one
// package+op per call, no batch form, fired through exec_batch for bounded
// concurrency instead of a strictly sequential fork loop.
static void apply_appops(const Json& cfg, RulesResult& res) {
    if (!cfg.contains("appops") || !cfg["appops"].is_array()) return;

    struct Entry {
        std::string pkg;
        std::string op;
    };

    std::vector<Entry> entries;
    std::vector<std::vector<std::string>> commands;

    for (auto& entry : cfg["appops"]) {
        if (!entry.value("enabled", true)) continue;

        std::string pkg  = entry.value("package", "");
        std::string mode = entry.value("mode",    "ignore");
        if (pkg.empty()) continue;

        std::string op;
        if (entry.contains("op") && entry["op"].is_number_integer())
            op = std::to_string(entry["op"].get<int>());
        else if (entry.contains("op") && entry["op"].is_string())
            op = entry["op"].get<std::string>();
        else continue;

        entries.push_back({pkg, op});
        commands.push_back({"cmd", "appops", "set", pkg, op, mode});
    }

    if (commands.empty()) return;

    auto results = exec_batch(commands);

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        const auto& e = entries[i];

        if (r.ok()) {
            res.appops_ok++;
        } else {
            Logger::warn("rules: appops failed: " + e.pkg + " op=" + e.op);
            res.failed++;
        }
    }
}

RulesResult apply_rules(
    const Json& cfg,
    std::string_view hash_cache_path,
    bool force) noexcept
{
    RulesResult res;

    try {
        // Require explicit source path — avoids ambiguous fallback behaviour
        if (!cfg.contains("_source_path") || !cfg["_source_path"].is_string()) {
            Logger::err("rules: _source_path missing — cannot hash, aborting");
            return res;
        }

        std::string src_path = cfg["_source_path"].get<std::string>();
        const auto current_hash = hashutil::hash_file(src_path);

        if (!current_hash) {
            Logger::err("rules: cannot hash " + src_path + " — file missing or unreadable");
            return res;
        }

        if (!force) {
            const auto saved = hashutil::read_hash_cache(hash_cache_path);
            if (saved && *saved == *current_hash) {
                Logger::info("rules: unchanged, skipping");
                return res;
            }
        }

        // Note: this hash guard covers components + appops together — both
        // are persistent Android-side state (PackageManager's
        // enabled/disabled flag, and appops mode overrides) that survive
        // reboot on their own, so re-applying when rules.json hasn't
        // changed would just be redundant binder calls. This is distinct
        // from game_mode's downscale/log-cleanup, which Android resets
        // every boot and therefore always re-applies regardless of hash —
        // see apply_game_mode in game_mode.cpp.
        Logger::info("rules: applying...");
        apply_components(cfg, res);
        apply_appops(cfg, res);

        // Save hash after full pass — even partial success is recorded
        // so next boot skips if config still same. Atomic: tmp write + fsync + rename.
        if (!hashutil::write_hash_cache_atomic(hash_cache_path, *current_hash)) {
            Logger::warn("rules: failed to persist hash cache — next boot will re-apply");
        }

        Logger::info("rules: done — components=" + std::to_string(res.components_ok) +
                     " appops=" + std::to_string(res.appops_ok) +
                     " failed=" + std::to_string(res.failed));
        return res;
    } catch (...) {
        Logger::err("rules: unexpected exception during apply — aborting this pass");
        return res;
    }
}
