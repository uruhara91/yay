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

} // namespace hashutil