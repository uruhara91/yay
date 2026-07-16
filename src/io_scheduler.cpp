#include "io_scheduler.h"
#include "exec_util.h"
#include "logger.h"
#include <dirent.h>
#include <string>
#include <cstring>
#include <vector>

namespace {

// RAII wrapper for DIR* — same rationale as the FileDescriptor wrappers in
// exec_util.cpp / hash_util.cpp / logger.cpp / json_config.cpp: without it,
// an exception thrown mid-loop (e.g. std::string allocation failure) would
// leak the directory handle since the original code only closedir()'d on
// the normal fall-through path.
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

} // namespace

static bool try_write_scheduler(const std::string& qdir, const std::string& sched) {
    std::string sched_path = qdir + "/scheduler";
    std::string available  = sysfs_read(sched_path.c_str());
    if (available.empty()) return false;

    // The available-schedulers string looks like "mq-deadline [kyber] cfq".
    // A naive substring search for "deadline" would match inside "mq-deadline"
    // and waste a sysfs write that the kernel will reject. Check that the
    // scheduler name is surrounded by word delimiters (space, '[', ']') or
    // sits at the string boundary so "deadline" only matches when it is
    // actually present as its own entry, not as a suffix of another name.
    const auto pos = available.find(sched);
    if (pos == std::string::npos) return false;
    const bool start_ok = (pos == 0) ||
                          (available[pos - 1] == ' ') ||
                          (available[pos - 1] == '[');
    const size_t end = pos + sched.size();
    const bool end_ok = (end == available.size()) ||
                        (available[end] == ' ') ||
                        (available[end] == ']');
    if (!start_ok || !end_ok) return false;

    return sysfs_write(sched_path.c_str(), sched.c_str());
}

IOResult apply_io_scheduler(const Json& cfg) {
    IOResult res;

    std::vector<std::string> pref_order = {"kyber", "mq-deadline", "deadline", "cfq"};

    if (cfg.contains("scheduler_preference") && cfg["scheduler_preference"].is_array()) {
        pref_order.clear();
        for (auto& s : cfg["scheduler_preference"])
            if (s.is_string()) pref_order.push_back(s.get<std::string>());
    }

    // read_ahead_kb: optional, applied to real block devices only
    std::string read_ahead;
    if (cfg.contains("read_ahead_kb") && cfg["read_ahead_kb"].is_number_integer())
        read_ahead = std::to_string(cfg["read_ahead_kb"].get<int>());

    DirHandle dir(opendir("/sys/block"));
    if (!dir.valid()) {
        Logger::err("io_scheduler: cannot open /sys/block");
        return res;
    }

    struct dirent* entry;
    while ((entry = readdir(dir.get())) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        std::string dev  = entry->d_name;
        std::string qdir = std::string("/sys/block/") + dev + "/queue";

        // Skip virtual/memory-backed devices — schedulers don't apply
        if (dev.rfind("ram",  0) == 0 ||
            dev.rfind("zram", 0) == 0 ||
            dev.rfind("dm-",  0) == 0) {
            res.skipped++;
            continue;
        }

        // loop*: no scheduling needed, set none + disable stats
        if (dev.rfind("loop", 0) == 0) {
            static_cast<void>(sysfs_write(qdir + "/scheduler", "none"));
            static_cast<void>(sysfs_write(qdir + "/iostats", "0"));
            static_cast<void>(sysfs_write(qdir + "/add_random", "0"));
            res.configured++;
            continue;
        }

        // Real storage: first available scheduler wins
        bool set = false;
        for (auto& sched : pref_order) {
            if (try_write_scheduler(qdir, sched)) {
                Logger::debug("io_scheduler: " + dev + " -> " + sched);
                set = true;
                break;
            }
        }

        if (!set) {
            Logger::warn("io_scheduler: " + dev + " — no suitable scheduler");
            res.failed++;
        } else {
            res.configured++;
        }

        // Disable iostat accounting (reduces per-I/O overhead)
        static_cast<void>(sysfs_write(qdir + "/iostats", "0"));
        // Don't contribute to kernel entropy pool from block events
        static_cast<void>(sysfs_write(qdir + "/add_random", "0"));

        if (!read_ahead.empty())
            static_cast<void>(sysfs_write(qdir + "/read_ahead_kb", read_ahead));
    }

    Logger::info("io_scheduler: configured=" + std::to_string(res.configured) +
                 " skipped="    + std::to_string(res.skipped)    +
                 " failed="     + std::to_string(res.failed));
    return res;
}
