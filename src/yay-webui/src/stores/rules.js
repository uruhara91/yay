import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { readConfig, writeConfig, applyRules, dryRunRules } from '@/helpers/bridge'

export const useRulesStore = defineStore('rules', () => {
  const components = ref([])
  const appops     = ref([])
  const loading    = ref(false)
  const saving     = ref(false)
  const error      = ref('')
  const searchQ    = ref('')

  const filteredComponents = computed(() =>
    searchQ.value
      ? components.value.filter(c =>
          c.package.includes(searchQ.value) ||
          c.component.includes(searchQ.value) ||
          (c.note ?? '').toLowerCase().includes(searchQ.value.toLowerCase())
        )
      : components.value
  )

  async function load() {
    loading.value = true
    error.value   = ''
    try {
      const cfg = await readConfig('rules.json')
      components.value = cfg.components ?? []
      appops.value     = cfg.appops     ?? []
    } catch (e) {
      error.value = e.message
    } finally {
      loading.value = false
    }
  }

  async function save() {
    saving.value = true
    try {
      await writeConfig('rules.json', {
        _comment: 'yay rules config. Set enabled:false to skip an entry without deleting it.',
        version: 1,
        components: components.value,
        appops:     appops.value,
      })
    } finally {
      saving.value = false
    }
  }

  async function saveAndApply() {
    await save()
    return applyRules('--force')
  }

  async function previewDryRun() {
    await save()
    return dryRunRules()
  }

  function addComponent(entry) {
    components.value.push({
      package: entry.package,
      component: entry.component,
      action: entry.action ?? 'disable',
      enabled: true,
      note: entry.note ?? '',
    })
  }

  function removeComponent(idx) { components.value.splice(idx, 1) }
  function toggleComponent(idx) {
    components.value[idx].enabled = !components.value[idx].enabled
  }

  function addAppop(entry) {
    const trimmedOp = String(entry.op).trim()
    const numericOp = /^-?\d+$/.test(trimmedOp) ? parseInt(trimmedOp, 10) : trimmedOp
    appops.value.push({
      package: entry.package,
      op:      numericOp,
      mode:    entry.mode ?? 'ignore',
      enabled: true,
      note:    entry.note ?? '',
    })
  }

  function removeAppop(idx) { appops.value.splice(idx, 1) }
  function toggleAppop(idx) {
    appops.value[idx].enabled = !appops.value[idx].enabled
  }

  return {
    components, appops, loading, saving, error, searchQ,
    filteredComponents,
    load, save, saveAndApply, previewDryRun,
    addComponent, removeComponent, toggleComponent,
    addAppop, removeAppop, toggleAppop,
  }
})
