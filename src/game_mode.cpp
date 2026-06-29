#include "game_mode.h"
#include "exec_util.h"
#include "logger.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <string>
#include <cstdio>

// Recursively delete all regular files inside a directory, keep the dir itself.
// Safer than bind-mount: no mount table footprint, no race with app startup.
static int purge_log_dir(const std::string& path) {
    DIR* d = opendir(path.c_str());
    if (!d) return 0;

    int removed = 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        std::string full = path + "/" + e->d_name;

        struct stat st;
        if (lstat(full.c_str(), &st) < 0) continue;

        if (S_ISREG(st.st_mode)) {
            if (remove(full.c_str()) == 0) removed++;
        } else if (S_ISDIR(st.st_mode)) {
            removed += purge_log_dir(full);
        }
        // Skip symlinks, special files — never follow them
    }
    closedir(d);
    return removed;
}

GameResult apply_game_mode(const Json& cfg) {
    GameResult res;

    if (!cfg.contains("games") || !cfg["games"].is_array()) {
        Logger::info("game_mode: no games configured");
        return res;
    }

    for (auto& game : cfg["games"]) {
        if (!game.value("enabled", true)) continue;

        std::string pkg = game.value("package", "");
        if (pkg.empty()) continue;

        // === DOWNSCALE via Android GameManager API ===
        // cmd game downscale <factor> <package>
        // Android 12+ (API 31). Factor: 0.0–1.0 (1.0 = native res).
        if (game.contains("downscale") && game["downscale"].is_number()) {
            double factor = game["downscale"].get<double>();
            if (factor > 0.0 && factor <= 1.0) {
                char factor_str[16];
                snprintf(factor_str, sizeof(factor_str), "%.2f", factor);
                auto r = exec_cmd({"cmd", "game", "downscale", factor_str, pkg});
                if (r.ok()) {
                    Logger::info("game_mode: downscale " + pkg + " -> " + factor_str);
                    res.downscale_set++;
                } else {
                    Logger::warn("game_mode: downscale failed for " + pkg +
                                 " (code=" + std::to_string(r.exit_code) + ")");
                    res.failed++;
                }
            }
        }

        // === LOG CLEANUP (replaces bind-mount /dev/null) ===
        // Periodic purge: delete contents of known log directories.
        // The game recreates empty log dirs on next launch — this is benign.
        if (game.value("cleanup_logs", false)) {
            if (game.contains("log_dirs") && game["log_dirs"].is_array()) {
                for (auto& dir_entry : game["log_dirs"]) {
                    if (!dir_entry.is_string()) continue;
                    std::string dir = dir_entry.get<std::string>();

                    struct stat st;
                    if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

                    int n = purge_log_dir(dir);
                    if (n > 0) {
                        Logger::info("game_mode: cleaned " + std::to_string(n) +
                                     " log files from " + dir);
                        res.logs_cleaned += n;
                    }
                }
            }
        }
    }

    Logger::info("game_mode: downscale_set=" + std::to_string(res.downscale_set) +
                 " logs_cleaned=" + std::to_string(res.logs_cleaned) +
                 " failed=" + std::to_string(res.failed));
    return res;
}
