# yay v1.0

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
exact mechanism and tunables.

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
