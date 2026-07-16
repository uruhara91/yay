#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using Json = nlohmann::json;

// Load JSON from file. Returns nullopt on parse error, missing file, or
// oversized file (>4 MB sanity cap).
[[nodiscard]]
std::optional<Json> load_json(std::string_view path) noexcept;

// Save JSON to file atomically (write to .tmp, fsync, then rename).
[[nodiscard]]
bool save_json(std::string_view path, const Json& j) noexcept;

// Lightweight structural sanity check for the three yay config files —
// intentionally not a full JSON-Schema validator (no extra dependency,
// nlohmann/json already does syntax validation in load_json). This exists
// to catch a config that *parses* fine as JSON but has the wrong shape —
// e.g. "components" present but not an array, or an array entry that's
// not an object — *before* it reaches apply_components/apply_appops/
// apply_game_mode, which already skip malformed individual entries but
// don't reject a structurally wrong file as a whole. Most valuable once
// rules.json/game_config.json/io_config.json may be written by a web UI
// rather than hand-edited: a bug on that side should surface as a clear
// rejected-config error, not partial silent skips deep in the apply path.
//
// Returns true if `cfg` has a sane top-level shape for `kind`. On failure,
// `error_out` (if non-null) is set to a short human-readable reason.
enum class ConfigKind { Rules, GameConfig, IoConfig };

[[nodiscard]]
bool validate_config_shape(
    const Json& cfg,
    ConfigKind kind,
    std::string* error_out = nullptr) noexcept;
