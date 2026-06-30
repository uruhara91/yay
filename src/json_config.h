#pragma once
#include "vendor/json.hpp"
#include <optional>
#include <string_view>

using Json = nlohmann::json;

// Load JSON from file. Returns nullopt on parse error, missing file, or
// oversized file (>4 MB sanity cap).
[[nodiscard]]
std::optional<Json> load_json(std::string_view path) noexcept;

// Save JSON to file atomically (write to .tmp, fsync, then rename).
[[nodiscard]]
bool save_json(std::string_view path, const Json& j) noexcept;
