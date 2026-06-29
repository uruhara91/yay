#include "json_config.h"
#include "logger.h"
#include <cstdio>
#include <cstring>
#include <string>

std::optional<Json> load_json(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return std::nullopt;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 4 * 1024 * 1024) { // 4 MB sanity cap
        fclose(f);
        return std::nullopt;
    }

    std::string buf(static_cast<size_t>(sz), '\0');
    if (fread(buf.data(), 1, sz, f) != static_cast<size_t>(sz)) {
        fclose(f);
        return std::nullopt;
    }
    fclose(f);

    try {
        return Json::parse(buf, nullptr, /*exceptions=*/true, /*ignore_comments=*/true);
    } catch (const Json::exception& e) {
        Logger::err(std::string("json_config: parse error in ") + path + ": " + e.what());
        return std::nullopt;
    }
}

bool save_json(const char* path, const Json& j) {
    std::string tmp = std::string(path) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;

    std::string out = j.dump(2);
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);

    if (!ok) { remove(tmp.c_str()); return false; }

    // Atomic rename — if power-cycles between write and rename, old file survives
    if (rename(tmp.c_str(), path) != 0) {
        remove(tmp.c_str());
        return false;
    }
    return true;
}
