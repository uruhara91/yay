// yay_inspect: on-demand helper for the webui's "add component" / "add
// appops override" pickers. Not part of the boot-time apply path — this is
// invoked lazily, only when the user opens one of those pickers, and exits
// immediately after printing its JSON result to stdout.
//
// Usage:
//   yay_inspect --components <package>   list activities/services/receivers/
//                                         providers declared by <package>,
//                                         read directly from its compiled
//                                         manifest (no dumpsys involved)
//   yay_inspect --appops [package]       list all known app-ops; if a
//                                         package is given, flag which ones
//                                         it has ever actually used
//
// Always exits 0 and prints a JSON object with an "error" key on failure —
// callers (the webui bridge) don't need to distinguish "no results" from
// "failed", both are just "nothing to show".

#include "appops_table.h"
#include "axml_parser.h"
#include "exec_util.h"
#include "logger.h"
#include "zip_reader.h"

#include <cstring>
#include <iostream>
#include <set>
#include <string>

namespace {

constexpr std::string_view kManifestEntryName = "AndroidManifest.xml";

[[nodiscard]]
std::string component_type_name(axml::ComponentType t) noexcept
{
    switch (t) {
        case axml::ComponentType::Activity: return "activity";
        case axml::ComponentType::Service:  return "service";
        case axml::ComponentType::Receiver: return "receiver";
        case axml::ComponentType::Provider: return "provider";
    }
    return "unknown";
}

// Resolves a package name to the path of its base APK via `pm path`, which
// is far cheaper than a full `dumpsys package` and works identically
// across Android versions/OEMs. A split APK (App Bundle install) reports
// multiple "package:" lines; the base one is always named "base.apk".
[[nodiscard]]
std::optional<std::string> find_base_apk_path(const std::string& package) noexcept
{
    auto result = exec_cmd({"pm", "path", package}, 5);
    if (!result.ok() || result.stdout_str.empty()) {
        return std::nullopt;
    }

    std::string best;
    size_t pos = 0;
    while (pos < result.stdout_str.size()) {
        size_t nl = result.stdout_str.find('\n', pos);
        std::string line = result.stdout_str.substr(
            pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? result.stdout_str.size() : nl + 1;

        constexpr std::string_view prefix = "package:";
        if (line.rfind(prefix, 0) != 0) continue;
        std::string path = line.substr(prefix.size());
        while (!path.empty() && (path.back() == '\r' || path.back() == '\n')) {
            path.pop_back();
        }
        if (path.empty()) continue;

        if (path.size() >= 8 && path.compare(path.size() - 8, 8, "base.apk") == 0) {
            return path; // exact match — this is definitely the base APK
        }
        if (best.empty()) best = path; // fallback: first path seen
    }

    return best.empty() ? std::nullopt : std::make_optional(best);
}

[[nodiscard]]
std::string json_escape(std::string_view s) noexcept
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

void print_error(std::string_view msg) noexcept
{
    std::cout << "{\"error\":\"" << json_escape(msg) << "\"}" << std::endl;
}

// ─── --components ───────────────────────────────────────────────────────
int run_components(const std::string& package)
{
    auto apk_path = find_base_apk_path(package);
    if (!apk_path) {
        print_error("package not found or pm path failed: " + package);
        return 0;
    }

    auto manifest_bytes = zip::extract_entry(*apk_path, kManifestEntryName);
    if (!manifest_bytes) {
        print_error("could not extract AndroidManifest.xml from " + *apk_path);
        return 0;
    }

    auto components = axml::parse_manifest_components(*manifest_bytes);

    std::cout << "{\"package\":\"" << json_escape(package) << "\",\"components\":[";
    bool first = true;
    for (const auto& c : components) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"type\":\"" << component_type_name(c.type) << "\","
                   << "\"name\":\"" << json_escape(c.name) << "\","
                   << "\"exported\":" << (c.exported ? "true" : "false") << ","
                   << "\"exported_explicit\":" << (c.exported_explicit ? "true" : "false") << ","
                   << "\"enabled\":" << (c.enabled ? "true" : "false")
                   << "}";
    }
    std::cout << "]}" << std::endl;
    return 0;
}

// ─── --appops ────────────────────────────────────────────────────────────

// `cmd appops get <pkg>` prints one line per op that has ever deviated
// from its default, e.g.:
//   COARSE_LOCATION: mode=foreground
//   START_FOREGROUND: mode=allow
// The name here is AppOpsManager's internal debug name (upper snake case,
// no "android:" prefix) — NOT the OPSTR_* string. We normalize
// "COARSE_LOCATION" -> "android:coarse_location" to match kKnownOps,
// since that's a mechanical transform (lowercase + prefix) that's stable
// regardless of the numeric op codes AOSP warns are unstable.
[[nodiscard]]
std::set<std::string> query_active_ops(const std::string& package) noexcept
{
    std::set<std::string> active;
    auto result = exec_cmd({"cmd", "appops", "get", package}, 10);
    if (!result.ok()) return active;

    size_t pos = 0;
    while (pos < result.stdout_str.size()) {
        size_t nl = result.stdout_str.find('\n', pos);
        std::string line = result.stdout_str.substr(
            pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? result.stdout_str.size() : nl + 1;

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        // Trim whitespace.
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
            name.erase(name.begin());
        }
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
            name.pop_back();
        }
        if (name.empty()) continue;

        std::string opstr = "android:";
        for (char c : name) {
            opstr += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        active.insert(opstr);
    }
    return active;
}

int run_appops(const std::string& package)
{
    std::set<std::string> active;
    if (!package.empty()) {
        active = query_active_ops(package);
    }

    std::cout << "{\"package\":\"" << json_escape(package) << "\",\"ops\":[";
    bool first = true;
    for (size_t i = 0; i < appops::kKnownOpsCount; ++i) {
        const auto& op = appops::kKnownOps[i];
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"op\":\"" << json_escape(op.opstr) << "\","
                   << "\"label\":\"" << json_escape(op.label) << "\","
                   << "\"category\":\"" << json_escape(op.category) << "\","
                   << "\"active\":" << (active.count(std::string(op.opstr)) ? "true" : "false")
                   << "}";
    }
    std::cout << "]}" << std::endl;
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    Logger::init("yay_inspect"); // logcat only — this never touches run.log

    if (argc < 2) {
        print_error("usage: yay_inspect --components <package> | --appops [package]");
        return 0;
    }

    if (std::strcmp(argv[1], "--components") == 0) {
        if (argc < 3 || argv[2][0] == '\0') {
            print_error("--components requires a package name");
            return 0;
        }
        return run_components(argv[2]);
    }

    if (std::strcmp(argv[1], "--appops") == 0) {
        std::string package = (argc >= 3) ? argv[2] : "";
        return run_appops(package);
    }

    print_error("unknown mode; use --components <package> or --appops [package]");
    return 0;
}
