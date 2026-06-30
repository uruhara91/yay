#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ExecResult {
    int         exit_code = -1;
    std::string stdout_str;

    [[nodiscard]] bool ok() const noexcept { return exit_code == 0; }
};

/// Run a command via execvp (no shell), capture stdout/stderr, return result.
/// @param argv Command and arguments (will be null-terminated internally)
/// @param timeout_sec Timeout in seconds (0 = no timeout, default 10s)
/// @return ExecResult with exit code and captured output
[[nodiscard]] ExecResult exec_cmd(
    const std::vector<std::string>& argv,
    int timeout_sec = 10) noexcept;

/// Write a string to a sysfs/procfs node atomically.
/// Handles partial writes by looping until all bytes are written.
/// @param path File system path to write to
/// @param value String value to write
/// @return true on success, false on error
[[nodiscard]] bool sysfs_write(std::string_view path, std::string_view value) noexcept;

/// Read a sysfs node and return contents (stripped of trailing whitespace).
/// @param path File system path to read from
/// @return Contents on success, empty string on error
[[nodiscard]] std::string sysfs_read(std::string_view path) noexcept;
