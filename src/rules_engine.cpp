#include "rules_engine.h"
#include "exec_util.h"
#include "hash_util.h"
#include "logger.h"
#include <string>
#include <string_view>
#include <unordered_set>
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

namespace {

// Android package/component identifiers are Java-style dotted identifiers:
// letters, digits, '.', '_', '$'. No whitespace, no '/', no control chars.
// This is a cheap sanity filter so a malformed config entry (stray
// newline, accidental path separator, empty segment) fails fast and loud
// here instead of being silently swallowed into a `cmd package disable`
// call that does something surprising or just errors out deep in
// system_server. This matters more once rules.json may be written by
// something other than a human editing JSON by hand (e.g. a future web
// UI) — config-driven strings shouldn't be trusted at face value before
// they become process arguments.
[[nodiscard]]
bool looks_like_valid_identifier(std::string_view s) noexcept {
    if (s.empty()) return false;
    for (char c : s) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '$';
        if (!ok) return false;
    }
    // Reject leading/trailing '.' and "..": cheap guard against malformed
    // input even though this never touches a filesystem path —
    // consistency with is_safe_purge_target's posture in game_mode.cpp.
    if (s.front() == '.' || s.back() == '.') return false;
    if (s.find("..") != std::string_view::npos) return false;
    return true;
}

} // namespace

static void apply_components(const Json& cfg, RulesResult& res, bool dry_run) {
    if (!cfg.contains("components") || !cfg["components"].is_array()) return;

    struct Entry {
        std::string target;     // "pkg/component", for logging
        std::string cmd_action; // "enable" or "disable"
    };

    std::vector<Entry> entries;
    std::vector<std::vector<std::string>> commands;
    std::unordered_set<std::string> seen; // dedup on "action pkg/component"

    int skipped_invalid = 0;
    int skipped_duplicate = 0;

    for (auto& entry : cfg["components"]) {
        if (!entry.value("enabled", true)) continue;

        std::string pkg  = entry.value("package",   "");
        std::string comp = entry.value("component",  "");
        std::string act  = entry.value("action",    "disable");

        if (pkg.empty() || comp.empty()) continue;

        if (!looks_like_valid_identifier(pkg) ||
            !looks_like_valid_identifier(comp)) {
            Logger::warn("rules: skipping malformed component entry: " +
                         pkg + "/" + comp);
            ++skipped_invalid;
            continue;
        }

        std::string cmd_action = (act == "enable") ? "enable" : "disable";
        std::string target = pkg + "/" + comp;

        const std::string dedup_key = cmd_action + " " + target;
        if (!seen.insert(dedup_key).second) {
            ++skipped_duplicate;
            continue;
        }

        entries.push_back({target, cmd_action});
        commands.push_back({"cmd", "package", cmd_action, target});
    }

    if (skipped_invalid > 0) {
        Logger::warn("rules: " + std::to_string(skipped_invalid) +
                     " component entries skipped (malformed)");
    }
    if (skipped_duplicate > 0) {
        Logger::info("rules: " + std::to_string(skipped_duplicate) +
                     " duplicate component entries skipped");
    }

    if (commands.empty()) return;

    if (dry_run) {
        Logger::info("rules: [dry-run] would apply " +
                     std::to_string(commands.size()) + " component change(s)");
        for (const auto& e : entries) {
            Logger::info("rules: [dry-run] cmd package " + e.cmd_action +
                         " " + e.target);
        }
        res.components_ok += static_cast<int>(entries.size());
        return;
    }

    Logger::info("rules: applying " + std::to_string(commands.size()) +
                 " component change(s)");

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
static void apply_appops(const Json& cfg, RulesResult& res, bool dry_run) {
    if (!cfg.contains("appops") || !cfg["appops"].is_array()) return;

    struct Entry {
        std::string pkg;
        std::string op;
        std::string mode;
    };

    std::vector<Entry> entries;
    std::vector<std::vector<std::string>> commands;
    std::unordered_set<std::string> seen; // dedup on "pkg op"

    int skipped_invalid = 0;
    int skipped_duplicate = 0;

    // appops mode is a small closed set in practice — allow/ignore/deny/
    // default — but Android's `cmd appops set` itself validates this, so
    // we only guard against obviously-wrong types here (op must resolve to
    // a plain non-negative integer string; mode/pkg must be sane
    // identifiers/words). This is intentionally permissive on `mode` since
    // AOSP has added new modes across versions and hardcoding the set here
    // would just bitrot.
    for (auto& entry : cfg["appops"]) {
        if (!entry.value("enabled", true)) continue;

        std::string pkg  = entry.value("package", "");
        std::string mode = entry.value("mode",    "ignore");
        if (pkg.empty() || mode.empty()) continue;

        if (!looks_like_valid_identifier(pkg)) {
            Logger::warn("rules: skipping malformed appops package: " + pkg);
            ++skipped_invalid;
            continue;
        }

        std::string op;
        if (entry.contains("op") && entry["op"].is_number_integer()) {
            const auto op_value = entry["op"].get<long long>();
            if (op_value < 0) {
                Logger::warn("rules: skipping negative appops op for " + pkg);
                ++skipped_invalid;
                continue;
            }
            op = std::to_string(op_value);
        } else if (entry.contains("op") && entry["op"].is_string()) {
            op = entry["op"].get<std::string>();
            if (!looks_like_valid_identifier(op)) {
                Logger::warn("rules: skipping malformed appops op for " + pkg);
                ++skipped_invalid;
                continue;
            }
        } else {
            continue;
        }

        const std::string dedup_key = pkg + " " + op;
        if (!seen.insert(dedup_key).second) {
            ++skipped_duplicate;
            continue;
        }

        entries.push_back({pkg, op, mode});
        commands.push_back({"cmd", "appops", "set", pkg, op, mode});
    }

    if (skipped_invalid > 0) {
        Logger::warn("rules: " + std::to_string(skipped_invalid) +
                     " appops entries skipped (malformed)");
    }
    if (skipped_duplicate > 0) {
        Logger::info("rules: " + std::to_string(skipped_duplicate) +
                     " duplicate appops entries skipped");
    }

    if (commands.empty()) return;

    if (dry_run) {
        Logger::info("rules: [dry-run] would apply " +
                     std::to_string(commands.size()) + " appops change(s)");
        for (const auto& e : entries) {
            Logger::info("rules: [dry-run] cmd appops set " + e.pkg +
                         " " + e.op + " " + e.mode);
        }
        res.appops_ok += static_cast<int>(entries.size());
        return;
    }

    Logger::info("rules: applying " + std::to_string(commands.size()) +
                 " appops change(s)");

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
    bool force,
    bool dry_run) noexcept
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

        // dry-run never consults or writes the hash cache: it's a preview
        // tool, not a real apply pass, and must always show the full
        // would-be effect of the current config regardless of whether it
        // "changed" since the last real apply.
        if (!force && !dry_run) {
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
        Logger::info(dry_run ? "rules: dry-run..." : "rules: applying...");
        apply_components(cfg, res, dry_run);
        apply_appops(cfg, res, dry_run);

        if (dry_run) {
            Logger::info("rules: [dry-run] done — would-apply components=" +
                         std::to_string(res.components_ok) +
                         " appops=" + std::to_string(res.appops_ok) +
                         " invalid=" + std::to_string(res.failed));
            return res;
        }

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
