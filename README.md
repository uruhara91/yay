# yay v1.1

Personal Android system tweaks as a Magisk module.  
**ARM64 only. Android 11 (API 30) minimum, optimized for Android 12+ (API 31).**

---

## Architecture

No persistent daemon. Binary runs, does its job, exits.

| Trigger | What runs | How |
|---|---|---|
| Install / update | `yay_apply --full` | `customize.sh` |
| Early boot (post-fs-data) | `yay_apply --post` | `post-fs-data.sh` |
| Boot completed | `yay_apply --boot` | `service.sh` |
| rules.json changed | `yay_apply --rules` | inotify watcher (opt-in) |
| game_config.json changed | `yay_apply --game` | inotify watcher (opt-in) |

Hash caching: `--rules` is skipped at boot if `rules.json` hasn't changed
since last run — component disable and appops mode overrides are persistent
Android-side state (they survive reboot on their own), so re-applying them
when nothing changed would just be redundant binder calls. `--game` and
`--io` are **not** hash-guarded and always run in full on every invocation:
downscale, log cleanup, the I/O scheduler, and TCP congestion control are
all runtime state that Android itself resets every boot, so they need to be
re-applied regardless of whether their config changed.

Every config file is also checked for basic *structural* validity (right
field types, arrays where arrays are expected) before it's handed to any
apply path — see "Config validation" below. A config that fails this check
is rejected outright and logged; nothing partial is applied from it.

---

## What it does

**post-fs-data (early boot)**
- `resetprop`: dex2oat compiler filter, debug flags, phantom process monitor

**boot**
- I/O scheduler: kyber → mq-deadline → deadline (first available per device)
- Disables iostat accounting and entropy contribution per block device
- TCP congestion control: cubic
- Component disable: analytics, measurement, OTA update receivers (see `config/rules.json`)
- Appops: per-package op overrides (see `config/rules.json`)
- Game mode: per-package resolution downscale via Android GameManager API
- fstrim: deferred 60s after boot

Component disable and appops calls each require one `cmd` invocation per
entry (Android's `pm`/`appops` shell commands don't support a batch form),
but they run with bounded concurrency (several at once, not strictly
one-at-a-time) and a per-item timeout, so a large `rules.json` doesn't turn
into a long serial wait — see `exec_batch()` in `src/exec_util.h` for the
exact mechanism and tunables. If spawning a command fails repeatedly in a
row (5 consecutive failures by default — almost always a sign the device is
out of file descriptors or process slots), the batch stops attempting
further spawns and fails the rest immediately rather than retrying in a
tight loop while resources are already under pressure.

---

## Config validation

Every load of `rules.json`, `game_config.json`, or `io_config.json` goes
through two layers before anything is acted on:

1. **Structural validation** (`validate_config_shape` in `json_config.cpp`)
   — catches a config that parses as valid JSON but has the wrong shape:
   `"components"` present but not an array, an array entry that isn't an
   object, a `package`/`component`/`op` field of the wrong type. A file
   that fails this check is rejected as a whole and logged with the
   specific reason — nothing partial is applied.
2. **Per-entry validation** (in `rules_engine.cpp`) — even a structurally
   valid file can contain individual entries with malformed values (stray
   whitespace, control characters, `..`, empty segments). These are
   skipped individually with a warning rather than passed through to
   `cmd package` / `cmd appops`, and duplicate entries (same package +
   component, or same package + op) are deduplicated so they aren't
   executed twice.

This matters most once config files may be written by something other
than a human hand-editing JSON — e.g. a future web UI — where a bug on
that side should surface as a clear rejected-config error in the log,
not as a partially-applied or silently-wrong run.

### Dry-run mode

`yay_apply --rules --dry-run` loads and validates `rules.json` exactly
like a real `--rules` run, but logs every `cmd package` / `cmd appops`
invocation it *would* make instead of actually running them — no
subprocess is spawned, and the hash cache is neither read nor written.
Useful for previewing the effect of a config change (by hand or from a
future web UI) before committing to it. Output goes to the normal log
(`/data/adb/yay/run.log` / `adb logcat -s yay`).

```sh
/data/adb/modules/yay/bin/yay_apply --rules --dry-run
```

`--dry-run` is only meaningful paired with `--rules`; it's ignored (with a
log warning) if combined with any other mode.

---

## Build

Requires Android NDK r25c+.

```sh
# Set NDK path (one of):
export ANDROID_NDK_HOME=~/Android/Sdk/ndk/26.x.x

# Build release binaries
./build.sh release

# Binaries land in bin/
# yay_apply  — one-shot applier
# yay_watch  — optional inotify watcher
```

Targets API 31, ABI arm64-v8a, C++20, static libc++ (NDK's `c++_static`).

nlohmann/json is pulled automatically via CMake `FetchContent` (pinned to a
tagged release) during configure — no manual vendoring step, and no
internet access needed beyond what `cmake -B` itself does on first
configure (cached afterward in the build directory).

---

## Configuration

All config lives in `/data/adb/yay/config/` after install.  
Edit JSON files directly. If inotify watcher is enabled, changes apply immediately.

### rules.json

```json
{
  "components": [
    {
      "package": "com.example.app",
      "component": "com.example.app.SomeService",
      "action": "disable",
      "enabled": true,
      "note": "optional comment"
    }
  ],
  "appops": [
    {
      "package": "com.example.app",
      "op": 40,
      "mode": "ignore",
      "enabled": true,
      "note": "op 40 = GET_USAGE_STATS"
    }
  ]
}
```

Set `"enabled": false` to skip an entry without deleting it.

`package` and `component` must look like ordinary Java-style dotted
identifiers (letters, digits, `.`, `_`, `$`, no leading/trailing dot, no
`..`) — anything else is skipped with a warning rather than passed to
`cmd package`. `op` may be an integer or a numeric string; negative values
are rejected.

### game_config.json

```json
{
  "games": [
    {
      "package": "com.example.game",
      "enabled": true,
      "downscale": 0.7,
      "cleanup_logs": true,
      "log_dirs": [
        "/data/media/0/Android/data/com.example.game/files/logs"
      ]
    }
  ]
}
```

`downscale`: 0.0–1.0 (1.0 = native resolution). Requires Android 12+ (API 31).  
`cleanup_logs`: purge contents of `log_dirs` on apply. No bind-mount, no mount table footprint.

`log_dirs` entries must resolve under the app's own
`/data/media/0/Android/{data,obb}/<package>/...` tree — anything else
(wrong app, wrong root, `..`, or just the app's storage root with nothing
under it) is rejected and logged instead of acted on.

### io_config.json

```json
{
  "scheduler_preference": ["kyber", "mq-deadline", "deadline", "cfq"],
  "read_ahead_kb": 128
}
```

---

## Optional: inotify watcher

The watcher (`yay_watch`) watches `/data/adb/yay/config/` for file changes and
calls `yay_apply` with the targeted mode. It blocks in `inotify_wait` — near-zero
CPU and memory overhead when idle.

To enable:
```sh
touch /data/adb/modules/yay/enable_watch
# Takes effect on next boot, or restart service.sh manually
```

To disable:
```sh
rm /data/adb/modules/yay/enable_watch
# Kill running watcher:
pkill yay_watch
```

---

## Logs

```
/data/adb/yay/run.log   — all apply runs
adb logcat -s yay       — real-time (tag: yay / yay_watch)
```

---

## Data paths

```
/data/adb/yay/
├── config/
│   ├── rules.json
│   ├── game_config.json
│   └── io_config.json
├── cache/
│   └── rules.hash      ← hash of last applied rules.json (rules.json only — see Hash caching above)
└── run.log
```

Config and cache survive module updates. Uninstalling Magisk module does NOT
delete `/data/adb/yay/` — remove manually if desired.

---

## Module structure

```
yay/
├── bin/
│   ├── yay_apply       ← compiled ARM64 binary
│   └── yay_watch       ← compiled ARM64 binary
├── config/             ← default configs (copied to /data/adb/yay/config/ on install)
│   ├── rules.json
│   ├── game_config.json
│   └── io_config.json
├── src/                ← C++20 source (not shipped in module zip)
├── CMakeLists.txt
├── build.sh
├── customize.sh
├── post-fs-data.sh
├── service.sh
└── module.prop
```

---

## Changelog

**v1.1**
- `rules.json` entries (`package`, `component`, `op`) are now validated
  against a strict identifier format before being passed to `cmd package` /
  `cmd appops`; malformed entries are skipped and logged instead of
  executed as-is.
- Duplicate `rules.json` entries (same target) are deduplicated before
  execution.
- New `validate_config_shape()` structural check runs on every config load
  (`rules.json`, `game_config.json`, `io_config.json`) — a file with the
  wrong top-level shape is rejected outright with a specific reason logged,
  instead of partially applying whatever happened to parse.
- New `yay_apply --rules --dry-run`: logs every command that would run
  without executing anything or touching the hash cache.
- `exec_batch()` now opens a circuit breaker after repeated consecutive
  spawn failures (default: 5) instead of retrying fork()/pipe2() in a tight
  loop under resource pressure.
- Minor clarity fix in `game_mode.cpp`'s `is_safe_purge_target` (no
  functional change).
