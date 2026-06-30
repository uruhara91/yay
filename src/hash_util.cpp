#include "hash_util.h"

#include <array>
#include <cinttypes>
#include <cstdio>
#include <memory>

namespace hashutil {

namespace {

using FilePtr = std::unique_ptr<FILE, decltype(&fclose)>;

constexpr uint64_t kSeed = 5381ULL;

[[nodiscard]]
uint64_t djb2_file(std::string_view path, bool& ok) noexcept {
    FilePtr fp(
        fopen(std::string(path).c_str(), "rb"),
        fclose);

    if (!fp) {
        ok = false;
        return 0;
    }

    std::array<uint8_t, 4096> buffer{};

    uint64_t hash = kSeed;

    while (true) {
        const size_t n =
            fread(buffer.data(), 1, buffer.size(), fp.get());

        if (n > 0) {
            for (size_t i = 0; i < n; ++i) {
                hash = ((hash << 5) + hash) ^ buffer[i];
            }
        }

        if (n < buffer.size()) {
            if (feof(fp.get()))
                break;

            if (ferror(fp.get())) {
                ok = false;
                return 0;
            }
        }
    }

    ok = true;
    return hash;
}

[[nodiscard]]
Hash to_hex(uint64_t value)
{
    char out[17];

    std::snprintf(
        out,
        sizeof(out),
        "%016" PRIx64,
        value);

    return Hash(out);
}

} // namespace

std::optional<Hash>
hash_file(std::string_view path) noexcept
{
    bool ok = false;

    const auto hash =
        djb2_file(path, ok);

    if (!ok)
        return std::nullopt;

    return to_hex(hash);
}

std::optional<Hash>
hash_files(std::span<const std::string_view> paths) noexcept
{
    uint64_t combined = kSeed;

    for (const auto path : paths) {

        bool ok = false;

        const auto hash =
            djb2_file(path, ok);

        if (!ok)
            return std::nullopt;

        combined ^=
            hash +
            0x9e3779b97f4a7c15ULL +
            (combined << 6) +
            (combined >> 2);
    }

    return to_hex(combined);
}

} // namespace hashutil