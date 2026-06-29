#include "hash_util.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <array>

// Simple but fast: read file, compute djb2 over contents.
// Good enough for change detection — not cryptographic, just identity.
static uint64_t djb2_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0ULL;

    uint64_t hash = 5381ULL;
    uint8_t  buf[4096];
    size_t   n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++)
            hash = ((hash << 5) + hash) ^ buf[i];
    }
    fclose(f);
    return hash;
}

static std::string u64_hex(uint64_t v) {
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
    return std::string(buf);
}

std::string hash_file(const char* path) {
    uint64_t h = djb2_file(path);
    if (h == 0ULL) return "";
    return u64_hex(h);
}

std::string hash_files(const char* const* paths, int count) {
    uint64_t combined = 5381ULL;
    for (int i = 0; i < count; i++) {
        uint64_t h = djb2_file(paths[i]);
        // Mix hashes — order-sensitive XOR + rotate
        combined ^= (h + 0x9e3779b97f4a7c15ULL + (combined << 6) + (combined >> 2));
    }
    return u64_hex(combined);
}
