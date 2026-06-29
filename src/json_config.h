#pragma once
#include "vendor/json.hpp"
#include <optional>
#include <string>

using Json = nlohmann::json;

// Load JSON from file. Returns nullopt on parse error or missing file.
std::optional<Json> load_json(const char* path);

// Save JSON to file atomically (write to .tmp, then rename).
bool save_json(const char* path, const Json& j);
