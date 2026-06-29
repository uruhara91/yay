#pragma once
#include <string>

// Returns hex MD5 of a file's contents, or "" on error.
// Used to detect config changes without re-reading the full file.
std::string hash_file(const char* path);

// Returns combined hash of multiple files (joined, then hashed).
std::string hash_files(const char* const* paths, int count);
