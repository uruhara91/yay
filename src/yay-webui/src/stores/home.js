import { defineStore } from 'pinia'
import { ref } from 'vue'
import { readConfig, isWatcherEnabled, bridgeName, PATHS } from '@/helpers/bridge'

export const useHomeStore = defineStore('home', () => {
  const version = ref('1.1')
  const componentCount = ref(0)
  const appopCount = ref(0)
  const gameCount = ref(0)
  const schedulerPreference = ref([])
  const watcherEnabled = ref(false)
  const bridge = ref('')
  const loading = ref(false)

  async function load() {
    loading.value = true
    bridge.value = bridgeName()
    try {
      const [rules, games, io, watcher] = await Promise.all([
        readConfig('rules.json').catch(() => ({ components: [], appops: [] })),
        readConfig('game_config.json').catch(() => ({ games: [] })),
        readConfig('io_config.json').catch(() => ({ scheduler_preference: [] })),
        isWatcherEnabled().catch(() => false),
      ])
      componentCount.value = (rules.components ?? []).filter((c) => c.enabled !== false).length
      appopCount.value = (rules.appops ?? []).filter((a) => a.enabled !== false).length
      gameCount.value = (games.games ?? []).filter((g) => g.enabled !== false).length
      schedulerPreference.value = io.scheduler_preference ?? []
      watcherEnabled.value = !!watcher
    } finally {
      loading.value = false
    }
  }

  return {
    version,
    componentCount,
    appopCount,
    gameCount,
    schedulerPreference,
    watcherEnabled,
    bridge,
    loading,
    load,
    PATHS,
  }
})
