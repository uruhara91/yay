import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { readConfig, writeConfig, applyGame } from '@/helpers/bridge'

export const useGamesStore = defineStore('games', () => {
  const games   = ref([])
  const loading = ref(false)
  const saving  = ref(false)
  const error   = ref('')
  const searchQ = ref('')

  const filtered = computed(() =>
    searchQ.value
      ? games.value.filter(g =>
          g.package.includes(searchQ.value) ||
          (g.note ?? '').toLowerCase().includes(searchQ.value.toLowerCase())
        )
      : games.value
  )

  async function load() {
    loading.value = true
    error.value   = ''
    try {
      const cfg = await readConfig('game_config.json')
      games.value = cfg.games ?? []
    } catch (e) {
      error.value = e.message
    } finally {
      loading.value = false
    }
  }

  async function save() {
    saving.value = true
    try {
      await writeConfig('game_config.json', {
        _comment: 'yay game mode config. downscale: 0.0-1.0 (1.0 = native res).',
        version: 1,
        games: games.value,
      })
    } finally {
      saving.value = false
    }
  }

  async function saveAndApply() {
    await save()
    return applyGame()
  }

  function addGame(g) {
    games.value.push({
      package: g.package,
      enabled: true,
      note: g.note ?? '',
      downscale: g.downscale ?? 0.7,
      cleanup_logs: g.cleanup_logs ?? false,
      log_dirs: g.log_dirs ?? [],
    })
  }

  function removeGame(idx)  { games.value.splice(idx, 1) }
  function toggleGame(idx)  { games.value[idx].enabled = !games.value[idx].enabled }

  /** Real index of a game object inside the unfiltered `games` array. */
  function indexOf(game) { return games.value.indexOf(game) }

  return {
    games, loading, saving, error, searchQ, filtered,
    load, save, saveAndApply,
    addGame, removeGame, toggleGame, indexOf,
  }
})
