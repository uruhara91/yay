#pragma once
#include "json_config.h"

struct RulesResult {
    int components_disabled = 0;
    int appops_set          = 0;
    int failed              = 0;
};

// Apply component disables and appops from rules.json.
// Hash-guarded: only runs if json actually changed since last run.
// hash_cache_path: where the last-run hash is persisted.
RulesResult apply_rules(const Json& cfg, const char* hash_cache_path, bool force = false);
