#pragma once
#include <string>
#include <vector>

struct ExecResult {
    int         exit_code;
    std::string stdout_str;
    bool ok() const { return exit_code == 0; }
};

// Run a command via execvp (no shell), capture stdout, return result.
// argv must be null-terminated. Timeout in seconds (0 = no timeout).
ExecResult exec_cmd(const std::vector<std::string>& argv, int timeout_sec = 10);

// Write a string to a sysfs/procfs node. Returns true on success.
bool sysfs_write(const char* path, const char* value);

// Read a sysfs node. Returns "" on error.
std::string sysfs_read(const char* path);
