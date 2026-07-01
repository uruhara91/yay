/**
 * Dual bridge: KernelSU WebUI (ksu.exec) + MMRL / WebUI X ($EnFile + webuix)
 * Falls back gracefully in browser for dev.
 */

// ── Runtime detection ──────────────────────────────────────────────────────
export const isKSU   = () => typeof ksu !== 'undefined'
export const isMMRL  = () => typeof $EnFile !== 'undefined'
export const isLive  = () => isKSU() || isMMRL()

// ── exec (KSU only; MMRL uses file ops) ───────────────────────────────────
export async function exec(cmd) {
  if (isKSU()) {
    return new Promise((resolve) => {
      ksu.exec(cmd, '', (errno, stdout, stderr) =>
        resolve({ errno, stdout: stdout ?? '', stderr: stderr ?? '' })
      )
    })
  }
  // Dev fallback
  console.debug('[bridge] exec:', cmd)
  return { errno: 0, stdout: '', stderr: '' }
}

// ── File I/O ───────────────────────────────────────────────────────────────
export async function readFile(path) {
  if (isMMRL() && $EnFile.exist(path)) {
    return $EnFile.read(path)
  }
  if (isKSU()) {
    const { errno, stdout, stderr } = await exec(`[ -f "${path}" ] && cat "${path}"`)
    if (errno !== 0) throw new Error(`readFile: ${stderr}`)
    return stdout
  }
  throw new Error('readFile: not running in KSU/MMRL')
}

export async function writeFile(path, content) {
  if (isMMRL()) {
    $EnFile.write(path, content)
    return
  }
  if (isKSU()) {
    const escaped = content.replace(/'/g, "'\\''")
    const { errno, stderr } = await exec(`printf '%s' '${escaped}' > "${path}"`)
    if (errno !== 0) throw new Error(`writeFile: ${stderr}`)
    return
  }
  throw new Error('writeFile: not running in KSU/MMRL')
}

export async function fileExists(path) {
  if (isMMRL()) return $EnFile.exist(path)
  if (isKSU()) {
    const { errno } = await exec(`[ -f "${path}" ]`)
    return errno === 0
  }
  return false
}

// ── yay-specific helpers ───────────────────────────────────────────────────
const CONFIG = '/data/adb/yay/config'
const LOG    = '/data/adb/yay/run.log'
const LOG1   = '/data/adb/yay/run.log.1'
const BIN    = '/data/adb/modules/yay/bin/yay_apply'

export const PATHS = { CONFIG, LOG, LOG1, BIN }

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
  const { errno, stdout, stderr } = await exec(`${BIN} --rules --dry-run 2>&1 || true`)
  return stdout || stderr
}

export async function readLog() {
  try { return await readFile(LOG) } catch { return '' }
}

export async function readPrevLog() {
  try { return await readFile(LOG1) } catch { return '' }
}
