#pragma once

#include <sys/types.h>

namespace renice {

// I/O scheduling class, mirrors the kernel's IOPRIO_CLASS_* values
// (linux/ioprio.h — not exposed by bionic's headers, so we define our own
// mirror rather than depend on a header that may or may not be present in
// a given NDK sysroot).
enum class IoClass {
    None = 0,        // inherits from CPU nice (io_prio = (nice + 20) / 5)
    Realtime = 1,     // first-in-line for disk time; can starve other
                      // processes if misused — intentionally not used by yay
    BestEffort = 2,   // default class for anything that asks explicitly;
                      // priority 0 (highest) .. 7 (lowest) within the class
    Idle = 3,         // only gets I/O time when nothing else wants it
};

// Sets both CPU nice value and I/O priority for a single process (not
// process group / thread group — just the one pid). Returns true only if
// *both* succeeded; check errno-derived context via the two out-params if
// finer-grained diagnostics are needed.
//
// `nice_value`: standard Linux nice range, -20 (highest priority) to 19
// (lowest). `ioprio_class`/`ioprio_level`: see IoClass above; level is
// only meaningful for BestEffort (0-7, lower = higher priority) and is
// ignored for Realtime/Idle/None.
struct RenicePidResult {
    bool nice_ok = false;
    bool ioprio_ok = false;
    int nice_errno = 0;
    int ioprio_errno = 0;

    [[nodiscard]] bool ok() const noexcept { return nice_ok && ioprio_ok; }
};

[[nodiscard]]
RenicePidResult renice_pid(
    pid_t pid,
    int nice_value,
    IoClass ioprio_class,
    int ioprio_level) noexcept;

} // namespace renice
