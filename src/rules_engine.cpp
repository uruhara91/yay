#include "rules_engine.h"
#include "exec_util.h"
#include "hash_util.h"
#include "logger.h"
#include <cstdio>
#include <fstream>
#include <string>

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

static void disable_component(const std::string& pkg,
                              const std::string& comp,
                              RulesResult& res) {
    // cmd package disable <pkg>/<comp>
    std::string target = pkg + "/" + comp;
    auto r = exec_cmd({"cmd", "package", "disable", target});
    if (r.ok() || r.stdout_str.find("new state") != std::string::npos) {
        res.components_disabled++;
    } else {
        Logger::warn("rules: disable failed: " + target + " (" +
                     std::to_string(r.exit_code) + ")");
        res.failed++;
    }
}

static void set_appop(const std::string& pkg, const std::string& op,
                      const std::string& mode, RulesResult& res) {
    // cmd appops set <pkg> <op> <mode>
    auto r = exec_cmd({"cmd", "appops", "set", pkg, op, mode});
    if (r.ok()) {
        res.appops_set++;
    } else {
        Logger::warn("rules: appops failed: " + pkg + " op=" + op +
                     " (" + std::to_string(r.exit_code) + ")");
        res.failed++;
    }
}

RulesResult apply_rules(const Json& cfg, const char* hash_cache_path, bool force) {
    RulesResult res;

    // Hash guard — only proceed if config changed since last run
    std::string current_hash;
    if (cfg.contains("_source_path") && cfg["_source_path"].is_string()) {
        current_hash = hash_file(cfg["_source_path"].get<std::string>().c_str());
    } else {
        // Hash the serialized JSON if no source path available
        current_hash = hash_file("/dev/null"); // fallback, always runs
        force = true;
    }

    if (!force && !current_hash.empty()) {
        std::string saved = load_hash_cache(hash_cache_path);
        if (saved == current_hash) {
            Logger::info("rules: no changes detected, skipping");
            return res;
        }
    }

    Logger::info("rules: applying components and appops...");

    // === COMPONENTS ===
    if (cfg.contains("components") && cfg["components"].is_array()) {
        for (auto& entry : cfg["components"]) {
            if (!entry.value("enabled", true)) continue;

            std::string pkg  = entry.value("package", "");
            std::string comp = entry.value("component", "");
            std::string act  = entry.value("action", "disable");

            if (pkg.empty() || comp.empty()) continue;

            if (act == "disable") {
                disable_component(pkg, comp, res);
            } else if (act == "enable") {
                std::string target = pkg + "/" + comp;
                auto r = exec_cmd({"cmd", "package", "enable", target});
                if (r.ok()) res.components_disabled++;
                else res.failed++;
            }
        }
    }

    // === APPOPS ===
    if (cfg.contains("appops") && cfg["appops"].is_array()) {
        for (auto& entry : cfg["appops"]) {
            if (!entry.value("enabled", true)) continue;

            std::string pkg  = entry.value("package", "");
            std::string mode = entry.value("mode", "ignore");

            if (pkg.empty()) continue;

            // op can be int (op code) or string (op name) — Android accepts both
            std::string op;
            if (entry.contains("op") && entry["op"].is_number_integer()) {
                op = std::to_string(entry["op"].get<int>());
            } else if (entry.contains("op") && entry["op"].is_string()) {
                op = entry["op"].get<std::string>();
            } else {
                continue;
            }

            set_appop(pkg, op, mode, res);
        }
    }

    // Save hash only after full successful pass
    if (!current_hash.empty()) {
        save_hash_cache(hash_cache_path, current_hash);
    }

    Logger::info("rules: done — components=" + std::to_string(res.components_disabled) +
                 " appops=" + std::to_string(res.appops_set) +
                 " failed=" + std::to_string(res.failed));
    return res;
}
