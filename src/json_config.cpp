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
