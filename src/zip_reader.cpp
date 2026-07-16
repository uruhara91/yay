#include "zip_reader.h"
#include "logger.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <zlib.h>

namespace zip {

namespace {

// ─── RAII fd wrapper (local copy — see hash_util.cpp for the sibling one;
// kept separate to avoid cross-TU coupling to that file's anonymous
// namespace type). ───────────────────────────────────────────────────────
class FileDescriptor {
public:
    explicit FileDescriptor(int fd = -1) noexcept : fd_(fd) {}
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.release()) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if (this != &other) reset(other.release());
        return *this;
    }
    ~FileDescriptor() noexcept { reset(); }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

    int release() noexcept
    {
        int f = fd_;
        fd_ = -1;
        return f;
    }

    void reset(int fd = -1) noexcept
    {
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
    }

private:
    int fd_;
};

// ─── ZIP on-disk structures (little-endian, packed) ─────────────────────
// Signatures and layouts per the ZIP APPNOTE.TXT spec. Only the fields we
// actually consume are named; padding comments mark the rest so offsets
// stay verifiable against the spec.

constexpr uint32_t kEocdSignature       = 0x06054b50;
constexpr uint32_t kCentralDirSignature = 0x02014b50;
constexpr uint32_t kLocalHeaderSignature = 0x04034b50;

constexpr uint16_t kMethodStore   = 0;
constexpr uint16_t kMethodDeflate = 8;

// Fixed part of the End Of Central Directory record (22 bytes), excluding
// the signature (already matched separately) and the variable-length
// comment that follows it.
#pragma pack(push, 1)
struct EocdFixed {
    uint16_t disk_number;
    uint16_t cd_start_disk;
    uint16_t cd_records_this_disk;
    uint16_t cd_records_total;
    uint32_t cd_size;
    uint32_t cd_offset;
    uint16_t comment_len;
};

struct CentralDirFixed {
    uint16_t version_made_by;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t name_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint16_t disk_start;
    uint16_t internal_attrs;
    uint32_t external_attrs;
    uint32_t local_header_offset;
};

struct LocalHeaderFixed {
    uint16_t version_needed;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t name_len;
    uint16_t extra_len;
};
#pragma pack(pop)

// Sanity ceiling: a decompressed AndroidManifest.xml is realistically a
// few hundred KB at most (largest real-world manifests — huge permission
// lists, hundreds of activities — still land well under 1 MB). Refuse
// anything absurd rather than let a corrupt/malicious size field drive an
// unbounded allocation.
constexpr uint32_t kMaxReasonableEntrySize = 8U * 1024U * 1024U; // 8 MiB

[[nodiscard]]
bool pread_exact(int fd, void* buf, size_t len, off_t offset) noexcept
{
    size_t total = 0;
    while (total < len) {
        ssize_t n = ::pread(
            fd,
            static_cast<uint8_t*>(buf) + total,
            len - total,
            offset + static_cast<off_t>(total));
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false; // short file
        total += static_cast<size_t>(n);
    }
    return true;
}

// Locate the End Of Central Directory record by scanning backward from
// EOF. The EOCD is a fixed 22-byte record optionally followed by a
// comment (0–65535 bytes), so it can't be more than ~64 KB + 22 bytes
// from the end of the file — no need to scan further than that.
[[nodiscard]]
std::optional<off_t> find_eocd(int fd, off_t file_size) noexcept
{
    constexpr size_t kMaxCommentLen = 65535;
    constexpr size_t kEocdFixedSize = 22; // signature(4) + EocdFixed(18)

    size_t scan_len = static_cast<size_t>(
        std::min<off_t>(file_size, static_cast<off_t>(kMaxCommentLen + kEocdFixedSize)));
    if (scan_len < kEocdFixedSize) return std::nullopt;

    std::vector<uint8_t> tail(scan_len);
    off_t start = file_size - static_cast<off_t>(scan_len);
    if (!pread_exact(fd, tail.data(), scan_len, start)) return std::nullopt;

    // Scan backward for the signature — comments can contain arbitrary
    // bytes, so this isn't airtight against a pathological comment
    // containing a fake signature, but that's the same trade-off every
    // ZIP reader makes; we're reading APKs signed/built by real
    // toolchains, not hostile archives.
    for (size_t i = scan_len - kEocdFixedSize + 1; i-- > 0;) {
        uint32_t sig;
        std::memcpy(&sig, &tail[i], sizeof(sig));
        if (sig == kEocdSignature) {
            return start + static_cast<off_t>(i);
        }
    }
    return std::nullopt;
}

[[nodiscard]]
bool inflate_raw(
    const uint8_t* src, size_t src_len,
    size_t dst_len, std::string* out) noexcept
{
    out->resize(dst_len);

    z_stream strm{};
    // -15 window bits: raw deflate stream, no zlib/gzip header — that's
    // what's stored inline in a ZIP entry.
    if (inflateInit2(&strm, -15) != Z_OK) return false;

    strm.next_in   = const_cast<uint8_t*>(src);
    strm.avail_in  = static_cast<uInt>(src_len);
    strm.next_out  = reinterpret_cast<Bytef*>(out->data());
    strm.avail_out = static_cast<uInt>(dst_len);

    int ret = ::inflate(&strm, Z_FINISH);
    ::inflateEnd(&strm);

    // Z_STREAM_END is the only fully-correct outcome; accept it only.
    return ret == Z_STREAM_END && strm.total_out == dst_len;
}

} // namespace

std::optional<std::string> extract_entry(
    std::string_view apk_path,
    std::string_view entry_name) noexcept
{
    FileDescriptor fd(::open(std::string(apk_path).c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.valid()) {
        Logger::warn(std::string("zip: cannot open ") + std::string(apk_path));
        return std::nullopt;
    }

    struct stat st{};
    if (::fstat(fd.get(), &st) != 0 || st.st_size <= 0) {
        return std::nullopt;
    }
    const off_t file_size = st.st_size;

    auto eocd_off = find_eocd(fd.get(), file_size);
    if (!eocd_off) {
        Logger::warn("zip: no End-Of-Central-Directory found (corrupt APK?)");
        return std::nullopt;
    }

    EocdFixed eocd{};
    if (!pread_exact(fd.get(), &eocd, sizeof(eocd), *eocd_off + 4 /*skip signature*/)) {
        return std::nullopt;
    }

    // ZIP64 marker: cd_records_total/cd_offset saturated at 0xffff/0xffffffff
    // means the real values live in a ZIP64 EOCD locator we don't parse.
    // APKs this large (>4 GiB or >65535 central-dir entries) don't happen
    // in practice for the manifest-reading use case here, so we just bail
    // cleanly rather than half-support it.
    if (eocd.cd_records_total == 0xffff || eocd.cd_offset == 0xffffffffU) {
        Logger::warn("zip: ZIP64 central directory not supported");
        return std::nullopt;
    }

    off_t cd_pos = eocd.cd_offset;
    for (uint16_t i = 0; i < eocd.cd_records_total; ++i) {
        uint32_t sig = 0;
        if (!pread_exact(fd.get(), &sig, sizeof(sig), cd_pos)) return std::nullopt;
        if (sig != kCentralDirSignature) {
            Logger::warn("zip: central directory entry has bad signature");
            return std::nullopt;
        }

        CentralDirFixed cdh{};
        if (!pread_exact(fd.get(), &cdh, sizeof(cdh), cd_pos + 4)) return std::nullopt;

        const off_t name_off = cd_pos + 4 + static_cast<off_t>(sizeof(cdh));
        const off_t next_pos = name_off
            + cdh.name_len + cdh.extra_len + cdh.comment_len;

        bool match = false;
        if (cdh.name_len == entry_name.size()) {
            std::string name(cdh.name_len, '\0');
            if (pread_exact(fd.get(), name.data(), cdh.name_len, name_off)
                && name == entry_name) {
                match = true;
            }
        }

        if (!match) {
            cd_pos = next_pos;
            continue;
        }

        if (cdh.uncompressed_size > kMaxReasonableEntrySize
            || cdh.compressed_size > kMaxReasonableEntrySize) {
            Logger::warn("zip: entry size implausibly large, refusing");
            return std::nullopt;
        }
        if (cdh.local_header_offset == 0xffffffffU) {
            Logger::warn("zip: ZIP64 local header offset not supported");
            return std::nullopt;
        }

        // Read the local file header to find where the actual data
        // starts — its name/extra field lengths can differ from the
        // central directory's in theory, so we don't trust the central
        // directory's offsets alone for the data position.
        uint32_t local_sig = 0;
        if (!pread_exact(fd.get(), &local_sig, sizeof(local_sig), cdh.local_header_offset)) {
            return std::nullopt;
        }
        if (local_sig != kLocalHeaderSignature) {
            Logger::warn("zip: local file header has bad signature");
            return std::nullopt;
        }

        LocalHeaderFixed lh{};
        if (!pread_exact(
                fd.get(), &lh, sizeof(lh),
                cdh.local_header_offset + 4)) {
            return std::nullopt;
        }

        const off_t data_off = cdh.local_header_offset + 4
            + static_cast<off_t>(sizeof(lh)) + lh.name_len + lh.extra_len;

        if (data_off < 0 || data_off > file_size
            || static_cast<off_t>(cdh.compressed_size) > file_size - data_off) {
            Logger::warn("zip: entry data extends past end of file");
            return std::nullopt;
        }

        std::string compressed(cdh.compressed_size, '\0');
        if (!pread_exact(fd.get(), compressed.data(), cdh.compressed_size, data_off)) {
            return std::nullopt;
        }

        if (cdh.method == kMethodStore) {
            if (cdh.compressed_size != cdh.uncompressed_size) {
                Logger::warn("zip: STORE entry size mismatch");
                return std::nullopt;
            }
            return compressed;
        }

        if (cdh.method == kMethodDeflate) {
            std::string out;
            if (!inflate_raw(
                    reinterpret_cast<const uint8_t*>(compressed.data()),
                    compressed.size(),
                    cdh.uncompressed_size,
                    &out)) {
                Logger::warn("zip: inflate failed for entry");
                return std::nullopt;
            }
            return out;
        }

        Logger::warn(std::string("zip: unsupported compression method ")
                      + std::to_string(cdh.method));
        return std::nullopt;
    }

    return std::nullopt; // entry not found
}

} // namespace zip
