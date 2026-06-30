#pragma once
#include "json_config.h"
#include <string_view>

struct RulesResult {
    int components_ok = 0;
    int appops_ok     = 0;
    int failed        = 0;
};

// Apply component disables and appops from rules.json.
// Hash-guarded: skips if config unchanged since last run.
// Requires cfg["_source_path"] to be set before calling.
[[nodiscard]]
RulesResult apply_rules(
    const Json& cfg,
    std::string_view hash_cache_path,
    bool force = false) noexcept;
