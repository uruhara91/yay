#include "axml_parser.h"
#include "logger.h"

#include <cstdint>
#include <cstring>
#include <optional>

namespace axml {

namespace {

// ─── AXML chunk types (ResourceTypes.h in AOSP frameworks/base) ─────────
constexpr uint16_t kResStringPoolType        = 0x0001;
constexpr uint16_t kResXmlType                = 0x0003;
constexpr uint16_t kResXmlStartNamespaceType  = 0x0100;
constexpr uint16_t kResXmlEndNamespaceType    = 0x0101;
constexpr uint16_t kResXmlStartElementType    = 0x0102;
constexpr uint16_t kResXmlEndElementType      = 0x0103;
constexpr uint16_t kResXmlCdataType           = 0x0104;
constexpr uint16_t kResXmlResourceMapType     = 0x0180;

// Compiled attribute value type tags (Res_value::dataType).
constexpr uint8_t kTypeIntBoolean = 0x12;

#pragma pack(push, 1)
struct ChunkHeader {
    uint16_t type;
    uint16_t header_size;
    uint32_t size;
};

struct StringPoolHeader {
    uint32_t string_count;
    uint32_t style_count;
    uint32_t flags;
    uint32_t strings_start;
    uint32_t styles_start;
};

struct XmlNodeHeader {
    uint32_t line_number;
    uint32_t comment;
};

struct XmlStartElement {
    uint32_t ns;
    uint32_t name;
    uint32_t attribute_start;  // "attributeStart" (offset to attrs, always 0x14)
    uint16_t attribute_size;
    uint16_t attribute_count;
    uint16_t id_index;
    uint16_t class_index;
    uint16_t style_index;
};

struct XmlAttribute {
    uint32_t ns;
    uint32_t name;
    uint32_t raw_value; // string pool index, or 0xffffffff if not a literal
    uint16_t value_size;
    uint8_t  value_res0;
    uint8_t  value_data_type;
    uint32_t value_data;
};
#pragma pack(pop)

constexpr uint32_t kNoValue = 0xffffffffU;
constexpr uint32_t kFlagUtf8 = 1U << 8;

// ─── Small cursor over the manifest bytes — bounds-checked, never throws;
// every read returns false (via the `ok` output) on overrun so callers can
// bail cleanly instead of reading out of bounds. ─────────────────────────
class Cursor {
public:
    explicit Cursor(std::string_view data) noexcept : data_(data) {}

    [[nodiscard]] size_t pos() const noexcept { return pos_; }
    [[nodiscard]] size_t size() const noexcept { return data_.size(); }
    void seek(size_t p) noexcept { pos_ = p; }

    template <typename T>
    [[nodiscard]] bool read(T* out) noexcept
    {
        if (pos_ + sizeof(T) > data_.size()) return false;
        std::memcpy(out, data_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

    [[nodiscard]] bool skip(size_t n) noexcept
    {
        if (pos_ + n > data_.size()) return false;
        pos_ += n;
        return true;
    }

    [[nodiscard]] const uint8_t* ptr_at(size_t off) const noexcept
    {
        if (off > data_.size()) return nullptr;
        return reinterpret_cast<const uint8_t*>(data_.data()) + off;
    }

    [[nodiscard]] size_t remaining_from(size_t off) const noexcept
    {
        return off <= data_.size() ? data_.size() - off : 0;
    }

private:
    std::string_view data_;
    size_t pos_ = 0;
};

[[nodiscard]]
std::optional<std::vector<std::string>> parse_string_pool(
    Cursor& cur, size_t chunk_start, const ChunkHeader& header) noexcept
{
    StringPoolHeader sph{};
    if (!cur.read(&sph)) return std::nullopt;

    const bool utf8 = (sph.flags & kFlagUtf8) != 0;
    if (sph.style_count != 0) {
        // Styled strings never appear in manifest string pools in
        // practice (styling is a resources.arsc / layout-string concept).
        // If we ever see one, something is unusual enough to bail rather
        // than guess at layout.
        Logger::warn("axml: styled string pool unsupported");
        return std::nullopt;
    }

    std::vector<uint32_t> offsets(sph.string_count);
    for (auto& off : offsets) {
        if (!cur.read(&off)) return std::nullopt;
    }

    const size_t strings_base = chunk_start + sph.strings_start;

    std::vector<std::string> strings;
    strings.reserve(sph.string_count);

    for (uint32_t rel_off : offsets) {
        size_t abs_off = strings_base + rel_off;
        const uint8_t* p = cur.ptr_at(abs_off);
        if (!p) return std::nullopt;
        size_t avail = cur.remaining_from(abs_off);

        if (utf8) {
            // UTF-8 pool: one length byte (or two if high bit set) for
            // the UTF-16 length, then the same again for the UTF-8 byte
            // length, then that many bytes.
            if (avail < 1) return std::nullopt;
            size_t i = 0;
            auto read_len = [&](size_t& idx) -> std::optional<size_t> {
                if (idx >= avail) return std::nullopt;
                size_t len = p[idx++];
                if (len & 0x80) {
                    if (idx >= avail) return std::nullopt;
                    len = ((len & 0x7f) << 8) | p[idx++];
                }
                return len;
            };
            auto utf16_len = read_len(i);
            if (!utf16_len) return std::nullopt;
            auto utf8_len = read_len(i);
            if (!utf8_len) return std::nullopt;
            if (i + *utf8_len > avail) return std::nullopt;
            strings.emplace_back(reinterpret_cast<const char*>(p + i), *utf8_len);
        } else {
            // UTF-16LE pool: one length unit (or two if high bit set),
            // in UTF-16 code units, then that many 16-bit units.
            if (avail < 2) return std::nullopt;
            uint16_t lo;
            std::memcpy(&lo, p, 2);
            size_t idx = 2;
            size_t len = lo;
            if (len & 0x8000) {
                if (avail < 4) return std::nullopt;
                uint16_t hi;
                std::memcpy(&hi, p + 2, 2);
                len = ((static_cast<size_t>(lo) & 0x7fff) << 16) | hi;
                idx = 4;
            }
            if (idx + len * 2 > avail) return std::nullopt;

            // Naive UTF-16LE -> UTF-8 (manifest strings are always plain
            // ASCII/BMP identifiers and short human text — no need for a
            // general Unicode library for surrogate pairs beyond this).
            std::string out;
            out.reserve(len);
            for (size_t u = 0; u < len; ++u) {
                uint16_t code;
                std::memcpy(&code, p + idx + u * 2, 2);
                if (code < 0x80) {
                    out.push_back(static_cast<char>(code));
                } else if (code < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
            }
            strings.push_back(std::move(out));
        }
    }

    cur.seek(chunk_start + header.size);
    return strings;
}

[[nodiscard]]
bool is_component_tag(std::string_view tag, ComponentType* out) noexcept
{
    if (tag == "activity" || tag == "activity-alias") { *out = ComponentType::Activity; return true; }
    if (tag == "service")   { *out = ComponentType::Service;  return true; }
    if (tag == "receiver")  { *out = ComponentType::Receiver; return true; }
    if (tag == "provider")  { *out = ComponentType::Provider; return true; }
    return false;
}

// Resolves a manifest android:name value against the package, matching
// PackageManager's own resolution: a name starting with "." is appended
// to the package; a name with no "." at all is *also* treated as
// relative (rare in practice, but valid per the manifest schema).
[[nodiscard]]
std::string resolve_component_name(std::string_view pkg, std::string_view name) noexcept
{
    if (name.empty()) return std::string(name);
    if (name.front() == '.') {
        return std::string(pkg) + std::string(name);
    }
    if (name.find('.') == std::string_view::npos) {
        return std::string(pkg) + "." + std::string(name);
    }
    return std::string(name);
}

} // namespace

std::vector<Component> parse_manifest_components(std::string_view axml_bytes) noexcept
{
    std::vector<Component> result;

    Cursor cur(axml_bytes);

    ChunkHeader root{};
    if (!cur.read(&root) || root.type != kResXmlType) {
        Logger::warn("axml: not a binary XML file (bad root chunk)");
        return {};
    }

    std::vector<std::string> strings;
    std::string package_name;
    std::string current_tag;
    // Tracks whether we're inside <application> — component tags outside
    // it (e.g. inside <queries>, which also nests component-like child
    // names in some schemas — not currently, but defensively) are ignored.
    int application_depth = -1; // -1 = not yet seen
    int depth = 0;

    std::optional<Component> pending;
    bool in_manifest_root = false;

    while (cur.pos() < root.size) {
        size_t chunk_start = cur.pos();
        ChunkHeader ch{};
        if (!cur.read(&ch)) break;
        cur.seek(chunk_start); // rewind so each case re-reads the header

        switch (ch.type) {
        case kResStringPoolType: {
            cur.seek(chunk_start + sizeof(ChunkHeader));
            auto pool = parse_string_pool(cur, chunk_start, ch);
            if (!pool) return {};
            strings = std::move(*pool);
            cur.seek(chunk_start + ch.size);
            break;
        }
        case kResXmlResourceMapType:
        case kResXmlStartNamespaceType:
        case kResXmlEndNamespaceType:
        case kResXmlCdataType:
            // Not needed for component extraction — every attribute we
            // care about (name/exported/enabled) is a plain string or
            // inline boolean, never a namespaced/resource-mapped value in
            // real-world manifests.
            cur.seek(chunk_start + ch.size);
            break;

        case kResXmlStartElementType: {
            cur.seek(chunk_start + sizeof(ChunkHeader));
            XmlNodeHeader node{};
            if (!cur.read(&node)) return {};
            XmlStartElement el{};
            if (!cur.read(&el)) return {};

            if (el.name >= strings.size()) return {};
            std::string tag = strings[el.name];
            ++depth;

            if (tag == "manifest") {
                in_manifest_root = true;
            }
            if (tag == "application" && application_depth < 0) {
                application_depth = depth;
            }

            ComponentType ctype{};
            bool is_component = in_manifest_root
                && application_depth >= 0
                && depth == application_depth + 1
                && is_component_tag(tag, &ctype);

            std::optional<std::string> name_attr;
            std::optional<bool> exported_attr;
            std::optional<bool> enabled_attr;

            for (uint16_t i = 0; i < el.attribute_count; ++i) {
                XmlAttribute attr{};
                if (!cur.read(&attr)) return {};

                if (attr.name >= strings.size()) continue;
                const std::string& attr_name = strings[attr.name];

                std::optional<std::string> str_val;
                if (attr.raw_value != kNoValue) {
                    if (attr.raw_value >= strings.size()) continue;
                    str_val = strings[attr.raw_value];
                }

                if (tag == "manifest" && attr_name == "package" && str_val) {
                    package_name = *str_val;
                } else if (is_component && attr_name == "name") {
                    if (str_val) name_attr = *str_val;
                } else if (is_component && attr_name == "exported") {
                    if (str_val) {
                        exported_attr = (*str_val == "true");
                    } else if (attr.value_data_type == kTypeIntBoolean) {
                        exported_attr = (attr.value_data != 0);
                    }
                } else if (is_component && attr_name == "enabled") {
                    if (str_val) {
                        enabled_attr = (*str_val == "true");
                    } else if (attr.value_data_type == kTypeIntBoolean) {
                        enabled_attr = (attr.value_data != 0);
                    }
                }
            }

            if (is_component && name_attr && !name_attr->empty()) {
                Component c;
                c.type = ctype;
                c.name = resolve_component_name(package_name, *name_attr);
                c.exported_explicit = exported_attr.has_value();
                c.exported = exported_attr.value_or(false);
                c.enabled = enabled_attr.value_or(true);
                result.push_back(std::move(c));
            }

            cur.seek(chunk_start + ch.size);
            break;
        }

        case kResXmlEndElementType: {
            --depth;
            if (application_depth >= 0 && depth < application_depth) {
                application_depth = -1; // left <application>
            }
            cur.seek(chunk_start + ch.size);
            break;
        }

        default:
            // Unknown chunk type inside the XML chunk stream — skip by
            // declared size rather than fail outright; forward
            // compatibility with chunk types we don't need.
            cur.seek(chunk_start + ch.size);
            break;
        }

        if (ch.size == 0) break; // guard against an infinite loop on garbage
    }

    return result;
}

} // namespace axml
