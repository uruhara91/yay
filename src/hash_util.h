#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hashutil {

using Hash = std::string;

[[nodiscard]]
std::optional<Hash> hash_file(std::string_view path) noexcept;

[[nodiscard]]
std::optional<Hash> hash_files(
    std::span<const std::string_view> paths) noexcept;

[[nodiscard]]
std::optional<Hash> read_hash_cache(std::string_view path) noexcept;

[[nodiscard]]
bool write_hash_cache_atomic(
    std::string_view path,
    std::string_view hash) noexcept;

} // namespace hashutil
