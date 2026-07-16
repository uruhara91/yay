/**
 * In-memory mock filesystem, used only when the WebUI is opened outside
 * KernelSU/MMRL (e.g. `bun dev` in a plain browser tab). Lets the whole UI
 * be clicked through and demoed without a rooted device.
 */

const DEFAULT_RULES = {
  _comment: 'yay rules config. Set enabled:false to skip an entry without deleting it.',
  version: 1,
  components: [
    {
      package: 'com.android.vending',
      component: 'com.google.android.finsky.systemupdateactivity.SystemUpdateActivity',
      action: 'disable',
      enabled: true,
      note: 'Play Store system update UI',
    },
    {
      package: 'com.google.android.gms',
      component: 'com.google.android.gms.analytics.AnalyticsService',
      action: 'disable',
      enabled: true,
    },
  ],
  appops: [
    {
      package: 'com.google.android.gms',
      op: 40,
      mode: 'ignore',
      enabled: true,
      note: 'op 40 = GET_USAGE_STATS',
    },
  ],
}

const DEFAULT_GAMES = {
  _comment: 'yay game mode config. downscale: 0.0-1.0 (1.0 = native res).',
  version: 1,
  games: [
    {
      package: 'com.garena.game.df',
      enabled: true,
      note: 'Delta Force',
      downscale: 0.7,
      cleanup_logs: true,
      log_dirs: ['/data/media/0/Android/data/com.garena.game.df/files/logs'],
    },
    {
      package: 'com.mobile.legends',
      enabled: true,
      note: 'Mobile Legends',
      downscale: 0.7,
      cleanup_logs: false,
      log_dirs: [],
    },
  ],
}

const DEFAULT_IO = {
  _comment: 'yay I/O scheduler config. scheduler_preference: ordered list, first available wins.',
  version: 1,
  scheduler_preference: ['kyber', 'mq-deadline', 'deadline', 'cfq'],
  read_ahead_kb: 128,
}

const DEFAULT_LOG = `[07-01 08:12:03 I yay_apply.cpp:44] yay_apply --boot starting (v1.1)
[07-01 08:12:03 I io_scheduler.cpp:88] scheduler set to kyber for mmcblk0, sda
[07-01 08:12:03 I io_scheduler.cpp:112] entropy contribution disabled, iostat disabled
[07-01 08:12:03 I net_tune.cpp:31] tcp_congestion_control -> cubic
[07-01 08:12:04 I rules_engine.cpp:203] hash unchanged, skipping component/appops apply
[07-01 08:12:04 I game_mode.cpp:97] applied downscale for 2 package(s)
[07-01 08:12:04 D fstrim.cpp:18] fstrim scheduled in 60s
[07-01 08:13:04 I fstrim.cpp:41] fstrim completed on /data (3.2s)
[07-01 08:13:04 I yay_apply.cpp:129] yay_apply --boot finished OK (1021ms)
`

const store = new Map()

function seed() {
  if (store.size) return
  store.set('/data/adb/yay/config/rules.json', JSON.stringify(DEFAULT_RULES, null, 2))
  store.set('/data/adb/yay/config/game_config.json', JSON.stringify(DEFAULT_GAMES, null, 2))
  store.set('/data/adb/yay/config/io_config.json', JSON.stringify(DEFAULT_IO, null, 2))
  store.set('/data/adb/yay/run.log', DEFAULT_LOG)
}
seed()

export function mockDelay(ms = 120) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

export const mockFs = {
  async read(path) {
    await mockDelay()
    if (!store.has(path)) throw new Error(`readFile: not found: ${path}`)
    return store.get(path)
  },
  async write(path, content) {
    await mockDelay()
    store.set(path, content)
  },
  async exists(path) {
    await mockDelay(30)
    return store.has(path)
  },
}
