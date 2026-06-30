#include "game_mode.h"
#include "exec_util.h"
#include "logger.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <string>
#include <cstdio>
#include <vector>

namespace {

// Roots under which log_dirs purges are allowed. Both are standard
// per-app storage locations on Android — owned by the app itself, safe to
// have their contents cleared (the app recreates them on next launch).
// Anything outside these roots is rejected outright, regardless of what
// game_config.json says: this guard exists specifically because log_dirs
// is user/webUI-editable config, not hardcoded — a typo or a bad paste
// here should never be able to reach outside app-private storage.
constexpr std::string_view kAllowedRoots[] = {
    "/data/media/0/Android/data/",
    "/data/media/0/Android/obb/",
};

// A purge target must additionally contain the owning package name as a
// path segment, so one game's log_dirs entry can't (even by mistake) point
// at another app's directory under the same allowed root.
[[nodiscard]]
bool path_contains_segment(const std::string& path, const std::string& segment) {
    if (segment.empty()) return false;

    size_t pos = 0;
    while ((pos = path.find(segment, pos)) != std::string::npos) {
        const bool start_ok = (pos == 0) || (path[pos - 1] == '/');
        const size_t end = pos + segment.size();
        const bool end_ok = (end == path.size()) || (path[end] == '/');
        if (start_ok && end_ok) return true;
        pos += 1;
    }
    return false;
}

// Reject anything that isn't a clean absolute path under an allowed root
// and scoped to `pkg`. No ".." traversal, no symlink-able ambiguity at the
// string level (lstat() in purge_dir() handles the filesystem-level
// symlink case once we're already inside a validated root).
[[nodiscard]]
bool is_safe_purge_target(const std::string& path, const std::string& pkg) {
    if (path.empty() || path[0] != '/') return false;
    if (path.find("..") != std::string::npos) return false;

    // Find the single allowed root this path actually starts with (a path
    // can only legitimately match one, since the roots are disjoint
    // prefixes) — avoids re-checking the "must have a segment after <pkg>"
    // rule against roots that don't even apply to this path.
    std::string_view matched_root;
    for (const auto root : kAllowedRoots) {
        if (path.rfind(root, 0) == 0) {
            matched_root = root;
            break;
        }
    }
    if (matched_root.empty()) return false;

    // Refuse to purge the app's storage root itself or anything too
    // shallow under it (e.g. "/data/media/0/Android/data/<pkg>" with
    // nothing after it) — log_dirs should point at a specific subdirectory,
    // not the whole app data tree.
    const std::string remainder = path.substr(matched_root.size());
    // remainder looks like "<pkg>" or "<pkg>/..." — require a path segment
    // after <pkg>.
    const auto slash = remainder.find('/');
    if (slash == std::string::npos || slash + 1 >= remainder.size()) {
        return false;
    }

    return path_contains_segment(path, pkg);
}

// RAII wrapper for DIR* — same rationale as the FileDescriptor wrappers
// elsewhere in this codebase: without it, an exception thrown mid-loop
// (e.g. std::string allocation failure) would leak the directory handle,
// since the original code only closedir()'d on the normal fall-through path.
// purge_dir() recurses, so this matters more here than a single-level scan:
// a leak here is a leak per directory level on a wedged recursive call.
class DirHandle {
public:
    explicit DirHandle(DIR* dir = nullptr) noexcept
        : dir_(dir)
    {
    }

    DirHandle(const DirHandle&) = delete;
    DirHandle& operator=(const DirHandle&) = delete;

    DirHandle(DirHandle&& other) noexcept
        : dir_(other.release())
    {
    }

    DirHandle& operator=(DirHandle&& other) noexcept
    {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    ~DirHandle() noexcept
    {
        reset();
    }

    [[nodiscard]]
    DIR* get() const noexcept
    {
        return dir_;
    }

    [[nodiscard]]
    bool valid() const noexcept
    {
        return dir_ != nullptr;
    }

    [[nodiscard]]
    DIR* release() noexcept
    {
        DIR* d = dir_;
        dir_ = nullptr;
        return d;
    }

    void reset(DIR* dir = nullptr) noexcept
    {
        if (dir_ != nullptr) {
            ::closedir(dir_);
        }
        dir_ = dir;
    }

private:
    DIR* dir_;
};

// Recursively delete regular files inside a directory tree, keep dirs intact.
// Safer than bind-mount: no mount table footprint, game recreates dirs on launch.
// Caller (apply_game_mode) is responsible for validating `path` via
// is_safe_purge_target() before this is ever invoked — this function trusts
// its caller, it does not re-validate, so it must never be made reachable
// with an unvalidated path.
int purge_dir(const std::string& path, int depth = 0) {
    if (depth > 8) return 0; // guard against symlink loops
    DirHandle d(opendir(path.c_str()));
    if (!d.valid()) return 0;

    int removed = 0;
    struct dirent* e;
    while ((e = readdir(d.get())) != nullptr) {
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
    return removed;
}

} // namespace

GameResult apply_game_mode(const Json& cfg) {
    GameResult res;

    if (!cfg.contains("games") || !cfg["games"].is_array()) {
        Logger::info("game_mode: no games configured");
        return res;
    }

    // No hash guard here, intentionally: downscale and log cleanup are both
    // runtime state that Android itself resets every boot (downscale is not
    // persisted by GameManager across reboots; log dirs refill as the game
    // runs), so this always re-applies on every call — boot, --game, or the
    // inotify watcher firing on a game_config.json edit. This is different
    // from apply_rules() in rules_engine.cpp, which guards on a hash because
    // component disable state and appops mode overrides *do* persist
    // Android-side across reboots and re-applying them when nothing changed
    // would just be redundant binder calls.
    for (auto& game : cfg["games"]) {
        if (!game.value("enabled", true)) continue;

        std::string pkg = game.value("package", "");
        if (pkg.empty()) continue;

        // === DOWNSCALE via Android GameManager API (API 31+) ===
        // cmd game downscale <factor> <package>
        // factor: 0.0–1.0 (1.0 = native res). Applied system-side, game unaware.
        // exit=255 typically means `cmd game` is not available on this ROM/API
        // level — not a hard failure worth retrying. We log stdout so the
        // exact Android error message appears in run.log for diagnosis.
        if (game.contains("downscale") && game["downscale"].is_number()) {
            double factor = game["downscale"].get<double>();
            if (factor > 0.0 && factor <= 1.0) {
                char fs[16];
                snprintf(fs, sizeof(fs), "%g", factor);
                if (fs[0] == '\0' || strchr(fs, 'e') != nullptr || strchr(fs, 'E') != nullptr) {
                    Logger::warn("game_mode: skipping downscale for " + pkg +
                                 " — could not format factor as plain decimal");
                    res.failed++;
                    continue;
                }
        
                auto r = exec_cmd({"cmd", "game", "downscale", fs, pkg});
                if (r.ok()) {
                    Logger::info("game_mode: " + pkg + " downscale=" + fs);
                    res.downscale_set++;
                } else {
                    std::string out = r.stdout_str;
                    while (!out.empty() &&
                           (out.back() == '\n' || out.back() == '\r' ||
                            out.back() == ' ')) {
                        out.pop_back();
                    }
                    Logger::warn("game_mode: downscale failed for " + pkg +
                                 " (exit=" + std::to_string(r.exit_code) +
                                 (out.empty() ? ")" : "): " + out));
                    res.failed++;
                }
            }
        }

        // === LOG CLEANUP ===
        // Purges log file contents on each apply call (boot / config change).
        // Game recreates log dirs on next launch — benign.
        //
        // log_dirs is config-driven (user-edited today, webUI-edited later),
        // so every entry is validated against is_safe_purge_target() before
        // purge_dir() ever touches it — a path outside the app's own
        // Android/data|obb tree, a path not scoped to this package, a path
        // containing "..", or a path that's just the app root itself with
        // nothing under it, is rejected and logged instead of acted on.
        if (game.value("cleanup_logs", false)) {
            if (!game.contains("log_dirs") || !game["log_dirs"].is_array()) continue;
            for (auto& dir_j : game["log_dirs"]) {
                if (!dir_j.is_string()) continue;
                std::string dir = dir_j.get<std::string>();

                if (!is_safe_purge_target(dir, pkg)) {
                    Logger::warn("game_mode: refusing unsafe log_dirs entry for " +
                                 pkg + ": " + dir);
                    res.failed++;
                    continue;
                }

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
