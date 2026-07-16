import { defineStore } from 'pinia'
import { ref } from 'vue'
import { readLog, readPrevLog } from '@/helpers/bridge'

export const useLogStore = defineStore('log', () => {
  const current  = ref('')
  const previous = ref('')
  const loading  = ref(false)
  const tab      = ref('current') // 'current' | 'previous'

  async function load() {
    loading.value = true
    try {
      ;[current.value, previous.value] = await Promise.all([readLog(), readPrevLog()])
    } finally {
      loading.value = false
    }
  }

  return { current, previous, loading, tab, load }
})
