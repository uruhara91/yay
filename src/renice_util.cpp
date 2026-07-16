#include "renice_util.h"

#include <cerrno>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace renice {

namespace {

// linux/ioprio.h is not part of bionic's public NDK headers, so these are
// mirrored directly from the kernel UAPI (stable ABI, unchanged since the
// syscall's introduction in 2.6.13 — safe to hardcode).
constexpr int kIoprioClassShift = 13;

[[nodiscard]]
constexpr int ioprio_value(int cls, int data) noexcept {
    return (cls << kIoprioClassShift) | data;
}

// ioprio_set's syscall number is NOT part of the generic/portable syscall
// ABI — it differs by architecture. arm64 (yay's actual target — see
// build.sh's ABI setting) uses the modern generic syscall table where it's
// 30 (include/uapi/asm-generic/unistd.h, unchanged since introduction).
// x86_64 predates that generic table and has its own legacy-derived
// numbering where it's 251 (asm/unistd_64.h) — verified experimentally
// during local testing: calling ioprio_set with the *other* architecture's
// number returns EINVAL even though every argument is otherwise valid,
// which is exactly what happens if this file is ever built/run on x86_64
// (e.g. an emulator image) using the arm64 number. x86_64 support exists
// here purely so that scenario doesn't silently misbehave — the shipped
// module binary is arm64-v8a only.
#if defined(__aarch64__)
constexpr long kSysIoprioSet = 30;
#elif defined(__x86_64__)
constexpr long kSysIoprioSet = 251;
#else
#error "renice_util.cpp: ioprio_set syscall number not defined for this ABI — yay ships arm64-v8a only (see build.sh); add this architecture's number here if you have a genuine need to build for it"
#endif

[[nodiscard]]
int ioprio_set_syscall(int who, int which, int ioprio) noexcept {
    return static_cast<int>(::syscall(kSysIoprioSet, who, which, ioprio));
}

} // namespace

RenicePidResult renice_pid(
    pid_t pid,
    int nice_value,
    IoClass ioprio_class,
    int ioprio_level) noexcept
{
    RenicePidResult result;

    // PRIO_PROCESS + explicit pid: affects only this process, not its
    // thread group or process group — a renice targeted at exactly the pid
    // the caller asked for and nothing wider.
    errno = 0;
    if (setpriority(PRIO_PROCESS, static_cast<id_t>(pid), nice_value) == 0) {
        result.nice_ok = true;
    } else {
        result.nice_errno = errno;
    }

    // ioprio_level only matters for BestEffort; clamp defensively so a
    // caller passing an out-of-range value (e.g. a config typo funneled
    // through here) can't produce an undefined/garbage priority value —
    // clamped to the kernel's documented 0 (highest) .. 7 (lowest) range.
    int level = ioprio_level;
    if (level < 0) level = 0;
    if (level > 7) level = 7;

    int cls_value = static_cast<int>(ioprio_class);
    int data = (ioprio_class == IoClass::BestEffort) ? level : 0;

    errno = 0;
    // IOPRIO_WHO_PROCESS = 1 (linux/ioprio.h) — targets exactly this pid,
    // same scoping rationale as PRIO_PROCESS above.
    constexpr int kIoprioWhoProcess = 1;
    if (ioprio_set_syscall(kIoprioWhoProcess, pid, ioprio_value(cls_value, data)) == 0) {
        result.ioprio_ok = true;
    } else {
        result.ioprio_errno = errno;
    }

    return result;
}

} // namespace renice
