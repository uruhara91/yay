#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace axml {

enum class ComponentType { Activity, Service, Receiver, Provider };

struct Component {
    ComponentType type;
    std::string   name;      // fully-qualified class name (already resolved
                              // against the manifest package + leading '.')
    bool          exported = false; // explicit android:exported value if
                                     // present; see `exported_explicit`
    bool          exported_explicit = false; // false = fell back to the
                                              // intent-filter-implied default
    bool          enabled = true;
};

// Parses a compiled AndroidManifest.xml (the binary AXML format found
// inside every APK — NOT text XML) and returns every <activity>,
// <service>, <receiver>, and <provider> declared directly under
// <application>. Component names are resolved to fully-qualified form
// (a leading "." is expanded against the manifest's package attribute,
// matching how PackageManager itself resolves android:name).
//
// This is intentionally narrow: no styles, no resource-reference
// resolution, no attributes beyond name/exported/enabled — anything
// requiring the resource table (arsc) is out of scope, since manifest
// component declarations essentially never use resource references for
// these particular attributes in practice.
//
// Returns an empty vector on any parse failure (never throws) — the
// caller (main_inspect) is expected to treat "no components found" and
// "failed to parse" the same way: nothing to show the user.
[[nodiscard]]
std::vector<Component> parse_manifest_components(std::string_view axml_bytes) noexcept;

} // namespace axml
