#pragma once

#include <chrono>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

constexpr std::chrono::seconds kDefaultExecTimeout{10};

struct ExecResult {
    int exit_code = -1;
    std::string stdout_str;

    [[nodiscard]]
    bool ok() const noexcept
    {
        return exit_code == 0;
    }
};

// Run a command via execvp (no shell), capture stdout, return result.
[[nodiscard]]
ExecResult exec_cmd(
    std::span<const std::string_view> argv,
    std::chrono::seconds timeout = kDefaultExecTimeout) noexcept;

[[nodiscard]]
ExecResult exec_cmd(
    std::initializer_list<std::string_view> argv,
    int timeout_sec = static_cast<int>(kDefaultExecTimeout.count())) noexcept;

// Write a string to a sysfs/procfs node. Returns true on success.
[[nodiscard]]
bool sysfs_write(std::string_view path, std::string_view value) noexcept;

// Read a sysfs node. Returns "" on error.
[[nodiscard]]
std::string sysfs_read(std::string_view path) noexcept;
