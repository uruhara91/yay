#include "io_scheduler.h"
#include "exec_util.h"
#include "logger.h"
#include <dirent.h>
#include <string>
#include <cstring>

static bool write_sched(const std::string& queue_path, const std::string& sched) {
    std::string sched_path = queue_path + "/scheduler";
    std::string available  = sysfs_read(sched_path.c_str());
    if (available.empty()) return false;
    if (available.find(sched) == std::string::npos) return false;
    return sysfs_write(sched_path.c_str(), sched.c_str());
}

IOResult apply_io_scheduler(const Json& cfg) {
    IOResult res;

    // Config: preferred scheduler order, fallback order
    std::vector<std::string> pref_order = {"kyber", "mq-deadline", "deadline", "cfq"};
    std::vector<std::string> loop_sched = {"none"};

    if (cfg.contains("scheduler_preference") && cfg["scheduler_preference"].is_array()) {
        pref_order.clear();
        for (auto& s : cfg["scheduler_preference"])
            if (s.is_string()) pref_order.push_back(s.get<std::string>());
    }

    DIR* dir = opendir("/sys/block");
    if (!dir) {
        Logger::err("io_scheduler: cannot open /sys/block");
        return res;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        std::string dev  = entry->d_name;
        std::string qdir = std::string("/sys/block/") + dev + "/queue";

        // Skip virtual/memory block devices
        if (dev.rfind("ram",  0) == 0 ||
            dev.rfind("zram", 0) == 0 ||
            dev.rfind("dm-",  0) == 0) {
            res.skipped++;
            continue;
        }

        // loop* gets "none" scheduler
        if (dev.rfind("loop", 0) == 0) {
            sysfs_write((qdir + "/scheduler").c_str(), "none");
            sysfs_write((qdir + "/iostats").c_str(),   "0");
            sysfs_write((qdir + "/add_random").c_str(), "0");
            res.configured++;
            continue;
        }

        // Real storage: try preferred schedulers in order
        bool set = false;
        for (auto& sched : pref_order) {
            if (write_sched(qdir, sched)) {
                Logger::debug("io_scheduler: " + dev + " -> " + sched);
                set = true;
                break;
            }
        }

        if (!set) {
            Logger::warn("io_scheduler: " + dev + " no suitable scheduler found");
            res.failed++;
        } else {
            res.configured++;
        }

        // Disable iostat accounting and entropy contribution — reduces overhead
        sysfs_write((qdir + "/iostats").c_str(),    "0");
        sysfs_write((qdir + "/add_random").c_str(), "0");

        // Read-ahead: use config value or keep default
        if (cfg.contains("read_ahead_kb") && cfg["read_ahead_kb"].is_number_integer()) {
            std::string ra = std::to_string(cfg["read_ahead_kb"].get<int>());
            sysfs_write((qdir + "/read_ahead_kb").c_str(), ra.c_str());
        }
    }
    closedir(dir);

    Logger::info("io_scheduler: configured=" + std::to_string(res.configured) +
                 " skipped=" + std::to_string(res.skipped) +
                 " failed=" + std::to_string(res.failed));
    return res;
}
