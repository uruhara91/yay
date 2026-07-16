#pragma once

#include <chrono>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

constexpr std::chrono::seconds kDefaultExecTimeout{10};

// Defaults for exec_batch — tuned for a personal-device workload (a couple
// hundred `cmd package` / `cmd appops` invocations, run rarely: on install
// and whenever rules.json actually changes, never on a no-op boot thanks to
// the hash guard upstream). Concurrency gives a real wall-clock win since
// `cmd` is I/O/IPC-bound, not CPU-bound. The total budget is a dead-man's
// switch, not a performance target — it exists purely so one wedged child
// can't hang the whole apply (and therefore service.sh/customize.sh) forever.
constexpr size_t kDefaultBatchConcurrency = 6U;
constexpr std::chrono::seconds kDefaultBatchItemTimeout{5};
constexpr std::chrono::seconds kDefaultBatchTotalBudget{5 * 60};

// If this many spawn attempts (fork/pipe2) fail back-to-back during a
// batch, stop trying to spawn further commands in that batch and fail the
// rest immediately. A run of consecutive failures almost always means the
// system is out of file descriptors or process slots — retrying in a tight
// loop won't fix that and just burns CPU while the device is already under
// resource pressure. A single isolated failure does not count against this
// (the counter resets on the next successful spawn).
constexpr size_t kDefaultMaxConsecutiveSpawnFailures = 5U;

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

// Run many independent commands with bounded concurrency and a single
// dead-man's-switch deadline for the whole batch (no shell, no ART, just
// fork/execvp like exec_cmd). Results are returned in the same order as
// `commands` so callers can map back to their source entries for logging.
//
// Semantics:
//   - Up to `max_concurrent` children alive at once.
//   - Each child gets up to `item_timeout` before being SIGKILLed
//     individually (same as exec_cmd's timeout) — most calls finish in
//     well under that; it's a ceiling, not a guaranteed wait.
//   - `total_budget` bounds the *entire* batch. If it's exhausted, any
//     children still running are killed and any commands not yet started
//     are skipped — reported as ExecResult{-1, ""} (ok() == false), never
//     silently treated as success.
//   - If `max_consecutive_spawn_failures` consecutive spawn attempts fail
//     (fork/pipe2 erroring, not a child exiting nonzero), remaining
//     unstarted commands are failed immediately without further spawn
//     attempts — see constant doc above.
//   - If `commands` is empty, returns an empty vector immediately.
[[nodiscard]]
std::vector<ExecResult> exec_batch(
    std::span<const std::vector<std::string>> commands,
    size_t max_concurrent = kDefaultBatchConcurrency,
    std::chrono::seconds item_timeout = kDefaultBatchItemTimeout,
    std::chrono::seconds total_budget = kDefaultBatchTotalBudget,
    size_t max_consecutive_spawn_failures = kDefaultMaxConsecutiveSpawnFailures) noexcept;

// Write a string to a sysfs/procfs node. Returns true on success.
[[nodiscard]]
bool sysfs_write(std::string_view path, std::string_view value) noexcept;

// Read a sysfs node. Returns "" on error.
[[nodiscard]]
std::string sysfs_read(std::string_view path) noexcept;
