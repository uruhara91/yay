#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace zip {

// Reads the central directory of a ZIP/APK file and extracts exactly one
// named entry's *decompressed* bytes. Not a general-purpose ZIP library —
// scoped to what yay_inspect needs: pull AndroidManifest.xml out of an APK
// without shelling out to `unzip` or `aapt`, and without a full ZIP
// dependency. Supports the only two compression methods Android itself
// recognizes for APK entries: STORE (0) and DEFLATE (8) — see
// https://unit42.paloaltonetworks.com/apk-badpack-malware-tampered-headers/
// for why those are the only two that matter here (the runtime treats
// anything else as STORE, but a manifest built by a real toolchain will
// only ever be one of these two).
//
// Returns nullopt if the archive can't be opened/parsed, or the entry
// isn't found, or its declared/actual sizes disagree (defends against a
// truncated read silently producing a partial-but-parseable manifest).
[[nodiscard]]
std::optional<std::string> extract_entry(
    std::string_view apk_path,
    std::string_view entry_name) noexcept;

} // namespace zip
