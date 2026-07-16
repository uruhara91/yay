#include "hash_util.h"

#include <array>
#include <cinttypes>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace hashutil {

namespace {

constexpr uint64_t kSeed = 5381ULL;
constexpr size_t kReadBufferSize = 4096U;
constexpr mode_t kCacheFileMode = 0600;

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
std::optional<FileDescriptor> open_readonly(std::string_view path) noexcept
{
    try {
        const std::string owned_path(path);
        FileDescriptor fd(::open(owned_path.c_str(), O_RDONLY | O_CLOEXEC));
        if (!fd.valid()) {
            return std::nullopt;
        }
        return fd;
    } catch (...) {
        return std::nullopt;
    }
}

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
std::optional<uint64_t> djb2_file(std::string_view path) noexcept
{
    auto fd = open_readonly(path);
    if (!fd) {
        return std::nullopt;
    }

    std::array<unsigned char, kReadBufferSize> buffer{};
    uint64_t hash = kSeed;

    while (true) {
        const auto n = ::read(fd->get(), buffer.data(), buffer.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::nullopt;
        }

        if (n == 0) {
            break;
        }

        const auto count = static_cast<size_t>(n);
        for (size_t i = 0; i < count; ++i) {
            hash = ((hash << 5U) + hash) ^ buffer[i];
        }
    }

    return hash;
}

[[nodiscard]]
Hash to_hex(uint64_t value)
{
    std::array<char, 17U> out{};

    std::snprintf(
        out.data(),
        out.size(),
        "%016" PRIx64,
        value);

    return Hash(out.data());
}

} // namespace

std::optional<Hash>
hash_file(std::string_view path) noexcept
{
    const auto hash = djb2_file(path);
    if (!hash) {
        return std::nullopt;
    }

    return to_hex(*hash);
}

std::optional<Hash>
hash_files(std::span<const std::string_view> paths) noexcept
{
    uint64_t combined = kSeed;

    for (const auto path : paths) {
        const auto hash = djb2_file(path);
        if (!hash) {
            return std::nullopt;
        }

        combined ^=
            *hash +
            0x9e3779b97f4a7c15ULL +
            (combined << 6U) +
            (combined >> 2U);
    }

    return to_hex(combined);
}

std::optional<Hash>
read_hash_cache(std::string_view path) noexcept
{
    auto fd = open_readonly(path);
    if (!fd) {
        return std::nullopt;
    }

    Hash hash;
    std::array<char, 128U> buffer{};

    while (true) {
        const auto n = ::read(fd->get(), buffer.data(), buffer.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::nullopt;
        }
        if (n == 0) {
            break;
        }

        hash.append(buffer.data(), static_cast<size_t>(n));
    }

    while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r')) {
        hash.pop_back();
    }

    return hash;
}

bool
write_hash_cache_atomic(std::string_view path, std::string_view hash) noexcept
{
    try {
        const std::filesystem::path cache_path{std::string(path)};
        const std::string cache_path_string = cache_path.string();
        const auto tmp_path = cache_path_string + ".tmp";

        FileDescriptor fd(::open(
            tmp_path.c_str(),
            O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
            kCacheFileMode));

        if (!fd.valid()) {
            return false;
        }

        const bool ok =
            write_all(fd.get(), hash) &&
            write_all(fd.get(), "\n") &&
            (::fsync(fd.get()) == 0);

        fd.reset();

        if (!ok) {
            static_cast<void>(::unlink(tmp_path.c_str()));
            return false;
        }

        if (::rename(tmp_path.c_str(), cache_path_string.c_str()) != 0) {
            static_cast<void>(::unlink(tmp_path.c_str()));
            return false;
        }

        return true;
    } catch (...) {
        return false;
    }
}

} // namespace hashutil
