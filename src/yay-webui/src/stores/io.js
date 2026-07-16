import { defineStore } from 'pinia'
import { ref } from 'vue'
import { readConfig, writeConfig, applyIO } from '@/helpers/bridge'

const KNOWN_SCHEDULERS = ['kyber', 'mq-deadline', 'deadline', 'bfq', 'cfq', 'noop', 'none']

export const useIoStore = defineStore('io', () => {
  const schedulerPreference = ref([...KNOWN_SCHEDULERS.slice(0, 4)])
  const readAheadKb = ref(128)
  const loading = ref(false)
  const saving = ref(false)
  const error = ref('')

  async function load() {
    loading.value = true
    error.value = ''
    try {
      const cfg = await readConfig('io_config.json')
      schedulerPreference.value = cfg.scheduler_preference?.length
        ? cfg.scheduler_preference
        : [...KNOWN_SCHEDULERS.slice(0, 4)]
      readAheadKb.value = cfg.read_ahead_kb ?? 128
    } catch (e) {
      error.value = e.message
    } finally {
      loading.value = false
    }
  }

  async function save() {
    saving.value = true
    try {
      await writeConfig('io_config.json', {
        _comment:
          'yay I/O scheduler config. scheduler_preference: ordered list, first available wins.',
        version: 1,
        scheduler_preference: schedulerPreference.value,
        read_ahead_kb: readAheadKb.value,
      })
    } finally {
      saving.value = false
    }
  }

  async function saveAndApply() {
    await save()
    return applyIO()
  }

  function moveUp(idx) {
    if (idx <= 0) return
    const arr = schedulerPreference.value
    ;[arr[idx - 1], arr[idx]] = [arr[idx], arr[idx - 1]]
  }

  function moveDown(idx) {
    const arr = schedulerPreference.value
    if (idx >= arr.length - 1) return
    ;[arr[idx + 1], arr[idx]] = [arr[idx], arr[idx + 1]]
  }

  function removeScheduler(idx) {
    schedulerPreference.value.splice(idx, 1)
  }

  function addScheduler(name) {
    const clean = name.trim().toLowerCase()
    if (!clean || schedulerPreference.value.includes(clean)) return
    schedulerPreference.value.push(clean)
  }

  return {
    schedulerPreference,
    readAheadKb,
    loading,
    saving,
    error,
    KNOWN_SCHEDULERS,
    load,
    save,
    saveAndApply,
    moveUp,
    moveDown,
    removeScheduler,
    addScheduler,
  }
})
