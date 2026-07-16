/**
 * Dual bridge: KernelSU WebUI + MMRL / WebUI X.
 *
 * Uses the official `kernelsu` and `webuix` npm packages (same approach as
 * Encore) rather than touching `window.ksu` / `window.webui` directly —
 * those packages already guard against the global being undefined and
 * expose a typed, promise-based API.
 *
 * WebUI X's file interface name is NOT a fixed global. Per MMRL's docs, a
 * module's own JS interfaces are exposed as `window.$<sanitizedModId>`
 * (fully deterministic — e.g. `yay` -> `window.$yay`), but its *file*
 * interface uses a short, host-assigned prefix (Encore -> `$EnFile`,
 * bindhosts -> `$BiFile`) that isn't derivable from modId by any documented
 * rule. So instead of guessing a prefix, we scan `window` for the
 * `$<Xx>File` pattern at runtime — exactly what `webuix`'s own internal
 * `WXClass` helper does — which works regardless of what prefix this
 * install happens to assign.
 *
 * Falls back to an in-memory mock when opened outside both bridges (e.g.
 * `vite dev` in a plain browser tab), so the whole UI stays clickable/
 * demoable without a rooted device.
 */

import { exec as ksuExec, toast as ksuToast } from 'kernelsu'
import { Intent, WebUI, WXEventHandler } from 'webuix'
import { mockFs, mockDelay } from './devMock'

const MOD_ID = 'yay'

// ── Runtime detection ───────────────────────────────────────────────────────

/** True when running inside KernelSU's (or a KSU-compatible) WebUI. */
export const isKSU = () => typeof window !== 'undefined' && typeof window.ksu !== 'undefined'

/**
 * True when running inside MMRL's WebUI X, detected via this module's own
 * scoped interface (`window.$yay`) — the one thing that's guaranteed to
 * exist under our exact module id, per MMRL's sanitizedModId contract.
 */
export const isWebUIX = () =>
  typeof window !== 'undefined' &&
  typeof window[`$${MOD_ID}`] !== 'undefined' &&
  Object.keys(window[`$${MOD_ID}`] ?? {}).length > 0

export const isLive = () => isKSU() || isWebUIX()

/** Human-readable bridge name for a small debug/status line in Settings. */
export function bridgeName() {
  if (isWebUIX()) return 'MMRL (WebUI X)'
  if (isKSU()) return 'KernelSU'
  return 'Browser (dev)'
}

// ── WebUI X file interface discovery ────────────────────────────────────────
// Mirrors webuix's own WXClass: find whichever `window.$XxFile` key exists,
// rather than assuming a specific prefix. Cached after first successful scan
// since the interface can't change mid-session.
let _fileInterfaceCache
function findFileInterface() {
  if (_fileInterfaceCache !== undefined) return _fileInterfaceCache
  if (typeof window === 'undefined') return (_fileInterfaceCache = null)

  const key = Object.keys(window).find((k) => /^\$\w{2}File$/.test(k))
  _fileInterfaceCache = key ? window[key] : null
  return _fileInterfaceCache
}

// ── exec ─────────────────────────────────────────────────────────────────
/**
 * Runs a shell command as root. Works when either bridge exposes a
 * KSU-compatible `window.ksu.exec` (true for KernelSU WebUI, and also true
 * for some MMRL WebView configurations) — falls back to a dev mock
 * otherwise so nothing throws outside a module context.
 */
export async function exec(cmd) {
  if (isKSU()) {
    try {
      const { errno, stdout, stderr } = await ksuExec(cmd)
      return { errno, stdout: stdout ?? '', stderr: stderr ?? '' }
    } catch (e) {
      return { errno: -1, stdout: '', stderr: String(e?.message ?? e) }
    }
  }

  console.debug('[bridge:dev] exec:', cmd)
  await mockDelay()
  return { errno: 0, stdout: '', stderr: '' }
}

/** Best-effort toast; silently does nothing outside KSU/dev. */
export function toast(message) {
  if (isKSU()) {
    try {
      ksuToast(message)
      return
    } catch (e) {
      console.warn('[bridge] toast failed:', e)
    }
  }
  console.debug('[bridge:dev] toast:', message)
}

/** Base64 helpers used to move file content across the exec boundary safely. */
function toBase64(str) {
  return btoa(unescape(encodeURIComponent(str)))
}
function fromBase64(b64) {
  return decodeURIComponent(escape(atob(b64)))
}

// ── File I/O ──────────────────────────────────────────────────────────────
export async function readFile(path) {
  const fi = findFileInterface()
  if (fi) {
    if (!fi.exists(path)) throw new Error(`readFile: not found: ${path}`)
    const content = fi.read(path)
    if (content === null) throw new Error(`readFile: failed to read: ${path}`)
    return content
  }

  if (isKSU()) {
    // base64 round-trip avoids any shell-quoting corruption on read.
    const { errno, stdout, stderr } = await exec(
      `[ -f "${path}" ] && base64 -w0 "${path}" || exit 3`,
    )
    if (errno !== 0) throw new Error(`readFile: not found: ${path}${stderr ? ` (${stderr})` : ''}`)
    try {
      return fromBase64(stdout.trim())
    } catch {
      const raw = await exec(`cat "${path}"`)
      if (raw.errno !== 0) throw new Error(`readFile: ${raw.stderr}`)
      return raw.stdout
    }
  }

  return mockFs.read(path)
}

export async function writeFile(path, content) {
  const fi = findFileInterface()
  if (fi) {
    fi.write(path, content)
    return
  }

  if (isKSU()) {
    const dir = path.slice(0, path.lastIndexOf('/'))
    const b64 = toBase64(content)
    // mkdir -p defensively, then decode base64 back into place — avoids all
    // shell-quoting pitfalls for JSON containing quotes/newlines/backslashes.
    const { errno, stderr } = await exec(
      `mkdir -p "${dir}" 2>/dev/null; echo '${b64}' | base64 -d > "${path}"`,
    )
    if (errno !== 0) throw new Error(`writeFile: ${stderr}`)
    return
  }

  return mockFs.write(path, content)
}

export async function fileExists(path) {
  const fi = findFileInterface()
  if (fi) return fi.exists(path)

  if (isKSU()) {
    const { errno } = await exec(`[ -f "${path}" ]`)
    return errno === 0
  }

  return mockFs.exists(path)
}

/**
 * Open a URL in the external browser. Prefers WebUI X's native Intent API
 * when available, falls back to `am start` via exec, then a plain
 * `window.open` in dev.
 */
export async function openWebsite(link) {
  if (isWebUIX()) {
    try {
      const webui = new WebUI()
      const intent = new Intent(Intent.ACTION_VIEW)
      intent.setData(link)
      webui.startActivity(intent)
      return
    } catch (e) {
      console.error('[bridge] openWebsite via WebUI X failed:', e)
    }
  }
  if (isKSU()) {
    const { errno } = await exec(`am start -a android.intent.action.VIEW -d "${link}"`)
    if (errno === 0) return
  }
  if (typeof window !== 'undefined') window.open(link, '_blank')
}

/**
 * List installed 3rd-party packages, for the "add game/app" pickers.
 * Returns [{ packageName }] — label is resolved lazily per-row via
 * getAppLabel, since batch label lookup differs a lot bridge-to-bridge.
 */
export async function listApps() {
  if (isKSU() && typeof window.ksu.listUserPackages === 'function') {
    try {
      return JSON.parse(window.ksu.listUserPackages()).map((packageName) => ({ packageName }))
    } catch (e) {
      console.error('[bridge] listUserPackages failed:', e)
    }
  }

  const { errno, stdout, stderr } = await exec('pm list packages -3')
  if (errno !== 0) throw new Error(`listApps: ${stderr}`)
  return stdout
    .split('\n')
    .map((l) => l.trim())
    .filter((l) => l.startsWith('package:'))
    .map((l) => ({ packageName: l.slice(8).trim() }))
    .filter((p) => p.packageName)
}

/** Best-effort app label lookup; falls back to the package name. */
export async function getAppLabel(packageName) {
  try {
    if (isKSU() && typeof window.ksu.getPackagesInfo === 'function') {
      const result = JSON.parse(window.ksu.getPackagesInfo(JSON.stringify([packageName])))
      if (result?.[0] && !result[0].error) return result[0].appLabel || packageName
    }
  } catch (e) {
    console.warn('[bridge] getAppLabel failed for', packageName, e)
  }
  return packageName
}

/**
 * Registers a hardware/gesture back handler via WebUI X's WXEventHandler.
 * No-op (returns a no-op unsubscribe) outside WebUI X.
 * @param {() => void} onBack
 * @returns {() => void} unsubscribe
 */
export function onHardwareBack(onBack) {
  if (!isWebUIX() || typeof window === 'undefined') return () => {}
  try {
    const handler = new WXEventHandler()
    return handler.on(window, 'back', onBack)
  } catch (e) {
    console.debug('[bridge] WXEventHandler back listener not available:', e)
    return () => {}
  }
}

// ── yay-specific helpers ────────────────────────────────────────────────────
const CONFIG = '/data/adb/yay/config'
const LOG = '/data/adb/yay/run.log'
const LOG1 = '/data/adb/yay/run.log.1'
const BIN = '/data/adb/modules/yay/bin/yay_apply'
const CACHE = '/data/adb/yay/cache/rules.hash'
const WATCH_FLAG = '/data/adb/modules/yay/enable_watch'
const MIRROR_PRIORITY_FLAG = '/data/adb/modules/yay/enable_mirror_priority'

export const PATHS = { CONFIG, LOG, LOG1, BIN, CACHE, WATCH_FLAG, MIRROR_PRIORITY_FLAG }

export async function readConfig(name) {
  const raw = await readFile(`${CONFIG}/${name}`)
  return JSON.parse(raw)
}

export async function writeConfig(name, obj) {
  await writeFile(`${CONFIG}/${name}`, JSON.stringify(obj, null, 2))
}

export async function applyRules(extra = '') {
  return exec(`${BIN} --rules ${extra}`.trim())
}

export async function applyGame() {
  return exec(`${BIN} --game`)
}

export async function applyIO() {
  return exec(`${BIN} --io`)
}

export async function applyFull() {
  return exec(`${BIN} --full`)
}

export async function dryRunRules() {
  const { stdout, stderr } = await exec(`${BIN} --rules --dry-run 2>&1`)
  return stdout || stderr
}

export async function readLog() {
  try {
    return await readFile(LOG)
  } catch {
    return ''
  }
}

export async function readPrevLog() {
  try {
    return await readFile(LOG1)
  } catch {
    return ''
  }
}

/** Whether the inotify watcher is enabled (presence of a flag file). */
export async function isWatcherEnabled() {
  try {
    return await fileExists(WATCH_FLAG)
  } catch {
    return false
  }
}

export async function setWatcherEnabled(enabled) {
  if (enabled) {
    await writeFile(WATCH_FLAG, '')
  } else {
    await exec(`rm -f "${WATCH_FLAG}"`)
  }
}

/**
 * Whether the mirroring-priority watcher (renices adbd/scrcpy-server when
 * a USB mirroring session — e.g. qtscrcpy — starts) is enabled. Independent
 * of the config-file watcher above: either can be on without the other,
 * both are read by the same yay_watch binary at its own startup (see
 * service.sh, which starts yay_watch if *either* sentinel is present).
 */
export async function isMirrorPriorityEnabled() {
  try {
    return await fileExists(MIRROR_PRIORITY_FLAG)
  } catch {
    return false
  }
}

export async function setMirrorPriorityEnabled(enabled) {
  if (enabled) {
    await writeFile(MIRROR_PRIORITY_FLAG, '')
  } else {
    await exec(`rm -f "${MIRROR_PRIORITY_FLAG}"`)
  }
}

// ── yay_inspect: on-demand component/appops browser ────────────────────────
// Backs the "Add component" / "Add appops override" pickers so the person
// doesn't have to manually dumpsys+grep over a terminal. Both read-only,
// invoked lazily (only when a picker modal opens), never during boot apply.
const INSPECT_BIN = '/data/adb/modules/yay/bin/yay_inspect'

// Basic Android package-name shape check before it ever reaches a shell
// command — not a security boundary (exec_util on the native side doesn't
// interpolate through a shell for this arg either), just a fast client-side
// guard against obviously-wrong input before spawning a process for it.
const PACKAGE_RE = /^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+$/

export async function inspectComponents(packageName) {
  if (!PACKAGE_RE.test(packageName)) {
    throw new Error(`invalid package name: ${packageName}`)
  }

  if (!isLive()) {
    await mockDelay()
    return mockInspectComponents(packageName)
  }

  const { errno, stdout, stderr } = await exec(`${INSPECT_BIN} --components "${packageName}"`)
  if (errno !== 0) throw new Error(`inspectComponents: ${stderr || 'yay_inspect failed'}`)

  let parsed
  try {
    parsed = JSON.parse(stdout)
  } catch {
    throw new Error('inspectComponents: could not parse yay_inspect output')
  }
  if (parsed.error) throw new Error(parsed.error)
  return parsed.components ?? []
}

export async function inspectAppops(packageName = '') {
  if (packageName && !PACKAGE_RE.test(packageName)) {
    throw new Error(`invalid package name: ${packageName}`)
  }

  if (!isLive()) {
    await mockDelay()
    return mockInspectAppops()
  }

  const cmd = packageName
    ? `${INSPECT_BIN} --appops "${packageName}"`
    : `${INSPECT_BIN} --appops`
  const { errno, stdout, stderr } = await exec(cmd)
  if (errno !== 0) throw new Error(`inspectAppops: ${stderr || 'yay_inspect failed'}`)

  let parsed
  try {
    parsed = JSON.parse(stdout)
  } catch {
    throw new Error('inspectAppops: could not parse yay_inspect output')
  }
  if (parsed.error) throw new Error(parsed.error)
  return parsed.ops ?? []
}

// Small representative fixture so the picker UI is fully clickable/
// demoable in a plain browser tab, without a device to actually read a
// manifest from.
function mockInspectComponents(packageName) {
  return [
    { type: 'activity', name: `${packageName}.MainActivity`, exported: true, exported_explicit: true, enabled: true },
    { type: 'activity', name: `${packageName}.SettingsActivity`, exported: false, exported_explicit: false, enabled: true },
    { type: 'service', name: `${packageName}.BackgroundSyncService`, exported: false, exported_explicit: true, enabled: true },
    { type: 'receiver', name: `${packageName}.BootReceiver`, exported: true, exported_explicit: true, enabled: true },
    { type: 'provider', name: `${packageName}.FileProvider`, exported: false, exported_explicit: true, enabled: true },
  ]
}

function mockInspectAppops() {
  return [
    { op: 'android:coarse_location', label: 'Coarse location', category: 'Location', active: true },
    { op: 'android:fine_location', label: 'Fine location', category: 'Location', active: false },
    { op: 'android:camera', label: 'Camera', category: 'Camera & Mic', active: false },
    { op: 'android:get_usage_stats', label: 'Get usage stats', category: 'Usage & Background', active: true },
    { op: 'android:system_alert_window', label: 'Draw over other apps', category: 'UI & Windows', active: false },
  ]
}
