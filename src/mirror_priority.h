#pragma once

namespace mirror_priority {

// Runs forever on the calling thread (intended to be spawned as a detached
// or joined background thread from yay_watch). Never polls in a tight loop:
//
//   1. Blocks in __system_property_wait() on "init.svc.adbd" — a genuine
//      kernel futex-based sleep, identical in spirit to inotify's poll()
//      wait elsewhere in this codebase. Costs nothing while idle and does
//      not prevent the device from reaching deep sleep, since nothing
//      periodically wakes the thread on its own.
//   2. The moment init.svc.adbd transitions to "running", renices adbd
//      itself immediately, then opens a short, bounded polling window
//      (a few seconds at a short interval) purely to catch scrcpy-server
//      the moment qtscrcpy pushes and starts it — there's no equivalent
//      property/event for an ad-hoc `app_process`-spawned process, so a
//      brief bounded scan is the only portable option (see project notes
//      on why netlink proc connector isn't available on this device's
//      kernel). The window closes as soon as scrcpy-server is found and
//      reniced, or its time budget elapses — whichever comes first.
//   3. Goes back to sleeping in __system_property_wait() for the next
//      adbd start.
//
// This never touches config files or run.log — matches the "silent,
// no logcat, no run.log" logging posture already chosen for the EEM
// undervolt experiment (see service.sh's commented-out block) for this
// same class of best-effort, opportunistic tweak.
void run() noexcept;

} // namespace mirror_priority
