#include "game_mode.h"
#include "exec_util.h"
#include "logger.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <string>
#include <cstdio>

// Recursively delete regular files inside a directory tree, keep dirs intact.
// Safer than bind-mount: no mount table footprint, game recreates dirs on launch.
static int purge_dir(const std::string& path, int depth = 0) {
    if (depth > 8) return 0; // guard against symlink loops
    DIR* d = opendir(path.c_str());
    if (!d) return 0;

    int removed = 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        std::string full = path + "/" + e->d_name;

        struct stat st;
        if (lstat(full.c_str(), &st) < 0) continue; // lstat: never follow symlinks

        if (S_ISREG(st.st_mode)) {
            if (remove(full.c_str()) == 0) removed++;
        } else if (S_ISDIR(st.st_mode)) {
            removed += purge_dir(full, depth + 1);
        }
        // Symlinks and special files: skip entirely
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

        // === DOWNSCALE via Android GameManager API (API 31+) ===
        // cmd game downscale <factor> <package>
        // factor: 0.0–1.0 (1.0 = native res). Applied system-side, game unaware.
        if (game.contains("downscale") && game["downscale"].is_number()) {
            double factor = game["downscale"].get<double>();
            if (factor > 0.0 && factor <= 1.0) {
                char fs[16];
                snprintf(fs, sizeof(fs), "%.2f", factor);
                auto r = exec_cmd({"cmd", "game", "downscale", fs, pkg});
                if (r.ok()) {
                    Logger::info("game_mode: " + pkg + " downscale=" + fs);
                    res.downscale_set++;
                } else {
                    Logger::warn("game_mode: downscale failed for " + pkg +
                                 " (exit=" + std::to_string(r.exit_code) + ")");
                    res.failed++;
                }
            }
        }

        // === LOG CLEANUP ===
        // Purges log file contents on each apply call (boot / config change).
        // Game recreates log dirs on next launch — benign.
        if (game.value("cleanup_logs", false)) {
            if (!game.contains("log_dirs") || !game["log_dirs"].is_array()) continue;
            for (auto& dir_j : game["log_dirs"]) {
                if (!dir_j.is_string()) continue;
                std::string dir = dir_j.get<std::string>();

                struct stat st;
                if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

                int n = purge_dir(dir);
                if (n > 0) {
                    Logger::info("game_mode: cleaned " + std::to_string(n) +
                                 " files from " + dir);
                    res.logs_cleaned += n;
                }
            }
        }
    }

    Logger::info("game_mode: downscale_set=" + std::to_string(res.downscale_set) +
                 " logs_cleaned=" + std::to_string(res.logs_cleaned) +
                 " failed="       + std::to_string(res.failed));
    return res;
}