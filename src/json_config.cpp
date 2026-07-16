#include "json_config.h"
#include "logger.h"

#include <cerrno>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr size_t kMaxJsonBytes = 4U * 1024U * 1024U; // 4 MB sanity cap
constexpr mode_t kJsonFileMode = 0600;

// RAII fd wrapper — identical contract to the one in hash_util.cpp /
// exec_util.cpp / logger.cpp. Kept as a local copy (header-only project,
// no shared internal target) rather than introducing a cross-TU dependency.
class FileDescriptor {
public:
    explicit FileDescriptor(int fd = -1) noexcept
        : fd_(fd)
    {
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept
        : fd_(other.release())
    {
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    ~FileDescriptor() noexcept
    {
        reset();
    }

    [[nodiscard]]
    int get() const noexcept
    {
        return fd_;
    }

    [[nodiscard]]
    bool valid() const noexcept
    {
        return fd_ >= 0;
    }

    [[nodiscard]]
    int release() noexcept
    {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

    void reset(int fd = -1) noexcept
    {
        if (fd_ >= 0) {
            while (::close(fd_) < 0 && errno == EINTR) {
            }
        }
        fd_ = fd;
    }

private:
    int fd_;
};

[[nodiscard]]
bool write_all(int fd, std::string_view value) noexcept
{
    const auto* data = value.data();
    size_t remaining = value.size();

    while (remaining > 0U) {
        const auto written = ::write(fd, data, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            return false;
        }

        const auto written_size = static_cast<size_t>(written);
        data += written_size;
        remaining -= written_size;
    }

    return true;
}

[[nodiscard]]
bool set_error(std::string* error_out, std::string msg) noexcept
{
    if (error_out) {
        try {
            *error_out = std::move(msg);
        } catch (...) {
            // best-effort — validation result itself is still correct
        }
    }
    return false;
}

// "components" entries in rules.json: each must be an object; "package"
// and "component" must be present and be strings if the entry is enabled
// (enabled defaults to true, matching apply_components' own default).
// Entries that are disabled are not required to be well-formed, since
// they're never acted on — mirrors how the apply path itself behaves.
[[nodiscard]]
bool validate_components_array(const Json& arr, std::string* error_out) noexcept
{
    size_t index = 0;
    for (const auto& entry : arr) {
        if (!entry.is_object()) {
            return set_error(error_out,
                "components[" + std::to_string(index) + "] is not an object");
        }

        const bool enabled = entry.value("enabled", true);
        if (enabled) {
            if (!entry.contains("package") || !entry["package"].is_string()) {
                return set_error(error_out,
                    "components[" + std::to_string(index) +
                    "].package missing or not a string");
            }
            if (!entry.contains("component") || !entry["component"].is_string()) {
                return set_error(error_out,
                    "components[" + std::to_string(index) +
                    "].component missing or not a string");
            }
        }
        ++index;
    }
    return true;
}

[[nodiscard]]
bool validate_appops_array(const Json& arr, std::string* error_out) noexcept
{
    size_t index = 0;
    for (const auto& entry : arr) {
        if (!entry.is_object()) {
            return set_error(error_out,
                "appops[" + std::to_string(index) + "] is not an object");
        }

        const bool enabled = entry.value("enabled", true);
        if (enabled) {
            if (!entry.contains("package") || !entry["package"].is_string()) {
                return set_error(error_out,
                    "appops[" + std::to_string(index) +
                    "].package missing or not a string");
            }
            if (!entry.contains("op") ||
                !(entry["op"].is_number_integer() || entry["op"].is_string())) {
                return set_error(error_out,
                    "appops[" + std::to_string(index) +
                    "].op missing or not an integer/string");
            }
        }
        ++index;
    }
    return true;
}

[[nodiscard]]
bool validate_games_array(const Json& arr, std::string* error_out) noexcept
{
    size_t index = 0;
    for (const auto& entry : arr) {
        if (!entry.is_object()) {
            return set_error(error_out,
                "games[" + std::to_string(index) + "] is not an object");
        }

        const bool enabled = entry.value("enabled", true);
        if (enabled) {
            if (!entry.contains("package") || !entry["package"].is_string()) {
                return set_error(error_out,
                    "games[" + std::to_string(index) +
                    "].package missing or not a string");
            }
        }

        if (entry.contains("downscale") && !entry["downscale"].is_null()) {
            bool ok = entry["downscale"].is_number()
                || (entry["downscale"].is_string()
                    && entry["downscale"].get<std::string>() == "disable");
            if (!ok) {
                return set_error(error_out,
                    "games[" + std::to_string(index) +
                    "].downscale present but not a number or \"disable\"");
            }
        }

        if (entry.contains("log_dirs")) {
            if (!entry["log_dirs"].is_array()) {
                return set_error(error_out,
                    "games[" + std::to_string(index) +
                    "].log_dirs present but not an array");
            }
            size_t dir_index = 0;
            for (const auto& dir_entry : entry["log_dirs"]) {
                if (!dir_entry.is_string()) {
                    return set_error(error_out,
                        "games[" + std::to_string(index) + "].log_dirs[" +
                        std::to_string(dir_index) + "] is not a string");
                }
                ++dir_index;
            }
        }
        ++index;
    }
    return true;
}

} // namespace

std::optional<Json> load_json(std::string_view path) noexcept
{
    try {
        const std::string owned_path(path);

        FileDescriptor fd(::open(owned_path.c_str(), O_RDONLY | O_CLOEXEC));
        if (!fd.valid()) {
            return std::nullopt;
        }

        struct stat st {};
        if (::fstat(fd.get(), &st) != 0) {
            return std::nullopt;
        }
        if (st.st_size <= 0 ||
            static_cast<size_t>(st.st_size) > kMaxJsonBytes) {
            return std::nullopt;
        }

        std::string buf(static_cast<size_t>(st.st_size), '\0');
        size_t total_read = 0;

        while (total_read < buf.size()) {
            const auto n = ::read(
                fd.get(),
                buf.data() + total_read,
                buf.size() - total_read);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return std::nullopt;
            }
            if (n == 0) {
                break; // file shrank concurrently — parse what we got
            }
            total_read += static_cast<size_t>(n);
        }
        buf.resize(total_read);

        return Json::parse(buf, nullptr, /*exceptions=*/true, /*ignore_comments=*/true);
    } catch (const Json::exception& e) {
        Logger::err(std::string("json_config: parse error in ") +
                    std::string(path) + ": " + e.what());
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

bool save_json(std::string_view path, const Json& j) noexcept
{
    try {
        const std::string owned_path(path);
        const std::string tmp_path = owned_path + ".tmp";

        FileDescriptor fd(::open(
            tmp_path.c_str(),
            O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
            kJsonFileMode));
        if (!fd.valid()) {
            return false;
        }

        const std::string out = j.dump(2);
        const bool ok =
            write_all(fd.get(), out) &&
            (::fsync(fd.get()) == 0);

        fd.reset();

        if (!ok) {
            ::unlink(tmp_path.c_str());
            return false;
        }

        if (::rename(tmp_path.c_str(), owned_path.c_str()) != 0) {
            ::unlink(tmp_path.c_str());
            return false;
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool validate_config_shape(
    const Json& cfg,
    ConfigKind kind,
    std::string* error_out) noexcept
{
    try {
        if (!cfg.is_object()) {
            return set_error(error_out, "top-level value is not an object");
        }

        switch (kind) {
            case ConfigKind::Rules: {
                if (cfg.contains("components")) {
                    if (!cfg["components"].is_array()) {
                        return set_error(error_out, "\"components\" is not an array");
                    }
                    if (!validate_components_array(cfg["components"], error_out)) {
                        return false;
                    }
                }
                if (cfg.contains("appops")) {
                    if (!cfg["appops"].is_array()) {
                        return set_error(error_out, "\"appops\" is not an array");
                    }
                    if (!validate_appops_array(cfg["appops"], error_out)) {
                        return false;
                    }
                }
                return true;
            }
            case ConfigKind::GameConfig: {
                if (cfg.contains("games")) {
                    if (!cfg["games"].is_array()) {
                        return set_error(error_out, "\"games\" is not an array");
                    }
                    if (!validate_games_array(cfg["games"], error_out)) {
                        return false;
                    }
                }
                return true;
            }
            case ConfigKind::IoConfig: {
                if (cfg.contains("scheduler_preference") &&
                    !cfg["scheduler_preference"].is_array()) {
                    return set_error(error_out,
                        "\"scheduler_preference\" is not an array");
                }
                if (cfg.contains("scheduler_preference")) {
                    size_t index = 0;
                    for (const auto& s : cfg["scheduler_preference"]) {
                        if (!s.is_string()) {
                            return set_error(error_out,
                                "scheduler_preference[" + std::to_string(index) +
                                "] is not a string");
                        }
                        ++index;
                    }
                }
                if (cfg.contains("read_ahead_kb") &&
                    !cfg["read_ahead_kb"].is_number_integer()) {
                    return set_error(error_out,
                        "\"read_ahead_kb\" is not an integer");
                }
                return true;
            }
        }

        return set_error(error_out, "unknown config kind");
    } catch (const Json::exception& e) {
        return set_error(error_out, std::string("exception during validation: ") + e.what());
    } catch (...) {
        return set_error(error_out, "unknown exception during validation");
    }
}
