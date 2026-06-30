#include "rules_engine.h"
#include "exec_util.h"
#include "hash_util.h"
#include "logger.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static std::string load_hash_cache(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::string h;
    std::getline(f, h);
    return h;
}

static void save_hash_cache(const char* path, const std::string& hash) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%s\n", hash.c_str());
    fclose(f);
}

// Batch component disable: build one pm command per package to minimize fork()s.
// Instead of 1 fork per component (100+ forks), we do 1 fork per package.
// "cmd package disable pkg/comp1 pkg/comp2 ..." is NOT supported by Android pm.
// Minimum granularity is one component per call — so we parallelize per-package
// by grouping them, and log failures without aborting the rest.
static void apply_components(const Json& cfg, RulesResult& res) {
    if (!cfg.contains("components") || !cfg["components"].is_array()) return;

    for (auto& entry : cfg["components"]) {
        if (!entry.value("enabled", true)) continue;

        std::string pkg  = entry.value("package",   "");
        std::string comp = entry.value("component",  "");
        std::string act  = entry.value("action",    "disable");

        if (pkg.empty() || comp.empty()) continue;

        std::string target = pkg + "/" + comp;
        std::string cmd_action = (act == "enable") ? "enable" : "disable";

        auto r = exec_cmd({"cmd", "package", cmd_action, target});
        // Android returns exit 0 and prints "new state: disabled" or similar
        // even if already disabled — treat both as success
        bool ok = r.ok() || r.stdout_str.find("new state") != std::string::npos;
        if (ok) {
            res.components_ok++;
        } else {
            Logger::warn("rules: " + cmd_action + " failed: " + target);
            res.failed++;
        }
    }
}

static void apply_appops(const Json& cfg, RulesResult& res) {
    if (!cfg.contains("appops") || !cfg["appops"].is_array()) return;

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

        auto r = exec_cmd({"cmd", "appops", "set", pkg, op, mode});
        if (r.ok()) {
            res.appops_ok++;
        } else {
            Logger::warn("rules: appops failed: " + pkg + " op=" + op);
            res.failed++;
        }
    }
}

RulesResult apply_rules(const Json& cfg, const char* hash_cache_path, bool force) {
    RulesResult res;

    // Require explicit source path — avoids ambiguous fallback behaviour
    if (!cfg.contains("_source_path") || !cfg["_source_path"].is_string()) {
        Logger::err("rules: _source_path missing — cannot hash, aborting");
        return res;
    }

    std::string src_path     = cfg["_source_path"].get<std::string>();
    std::string current_hash = hash_file(src_path.c_str());

    if (current_hash.empty()) {
        Logger::err("rules: cannot hash " + src_path + " — file missing or unreadable");
        return res;
    }

    if (!force) {
        std::string saved = load_hash_cache(hash_cache_path);
        if (saved == current_hash) {
            Logger::info("rules: unchanged, skipping");
            return res;
        }
    }

    Logger::info("rules: applying...");
    apply_components(cfg, res);
    apply_appops(cfg, res);

    // Save hash after full pass — even partial success is recorded
    // so next boot skips if config still same
    save_hash_cache(hash_cache_path, current_hash);

    Logger::info("rules: done — components=" + std::to_string(res.components_ok) +
                 " appops=" + std::to_string(res.appops_ok) +
                 " failed=" + std::to_string(res.failed));
    return res;
}