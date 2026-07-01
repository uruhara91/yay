<template>
  <div class="page h-full flex flex-col">
    <div class="sticky top-0 z-10 bg-background">
      <div class="max-w-2xl mx-auto px-5 pt-5 pb-2">
        <div class="flex items-center justify-between">
          <h1 class="text-xl font-semibold text-on-surface">Log</h1>
          <button @click="store.load()" title="Refresh"
            class="w-9 h-9 rounded-full bg-surface-container flex items-center justify-center
                   text-on-surface-variant active:bg-surface-container-high transition-colors">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor">
              <path d="M17.65 6.35A7.958 7.958 0 0012 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99 0 0112 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z"/>
            </svg>
          </button>
        </div>
        <!-- Tab -->
        <div class="flex mt-3 bg-surface-container rounded-xl p-1 gap-1">
          <button @click="store.tab = 'current'"
            class="flex-1 py-1.5 rounded-lg text-sm font-medium transition-colors"
            :class="store.tab === 'current'
              ? 'bg-secondary-container text-on-secondary-container'
              : 'text-on-surface-variant'">
            Current
          </button>
          <button @click="store.tab = 'previous'"
            class="flex-1 py-1.5 rounded-lg text-sm font-medium transition-colors"
            :class="store.tab === 'previous'
              ? 'bg-secondary-container text-on-secondary-container'
              : 'text-on-surface-variant'">
            Previous
          </button>
        </div>
        <!-- Filter chips -->
        <div class="flex gap-2 mt-2 overflow-x-auto scrollbar-hidden pb-1">
          <button v-for="f in filters" :key="f.key"
            @click="activeFilter = activeFilter === f.key ? '' : f.key"
            class="shrink-0 px-3 py-1 rounded-full text-xs font-medium border transition-colors"
            :class="activeFilter === f.key
              ? `${f.active}`
              : 'border-outline-variant text-on-surface-variant'">
            {{ f.label }}
          </button>
        </div>
      </div>
    </div>

    <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-auto">
      <div class="max-w-2xl mx-auto px-3 py-2">
        <LoadingSpinner v-if="store.loading" />
        <div v-else-if="!logText" class="text-center py-8 text-on-surface-variant text-sm">
          No log available
        </div>
        <div v-else class="bg-surface-container rounded-2xl p-3 overflow-x-auto">
          <div v-for="(line, i) in filteredLines" :key="i"
            class="text-xs font-mono leading-5 whitespace-pre-wrap break-all"
            :class="lineClass(line)">{{ line }}</div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useLogStore } from '@/stores/log'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'

const store = useLogStore()
const activeFilter = ref('')

const filters = [
  { key: 'E', label: '🔴 Error',   active: 'bg-error/15 border-error text-error' },
  { key: 'W', label: '🟡 Warn',    active: 'bg-yellow-400/15 border-yellow-400 text-yellow-400' },
  { key: 'I', label: '🔵 Info',    active: 'bg-primary/15 border-primary text-primary' },
  { key: 'D', label: '⚪ Debug',   active: 'bg-surface-container-high border-outline text-on-surface' },
]

const logText = computed(() =>
  store.tab === 'current' ? store.current : store.previous
)

const allLines = computed(() =>
  logText.value ? logText.value.split('\n').filter(Boolean) : []
)

const filteredLines = computed(() => {
  if (!activeFilter.value) return allLines.value
  const f = activeFilter.value
  return allLines.value.filter(l => l.includes(` ${f} `))
})

function lineClass(line) {
  if (line.includes(' E ')) return 'text-error'
  if (line.includes(' W ')) return 'text-yellow-400'
  if (line.includes(' I ')) return 'text-on-surface'
  if (line.includes(' D ')) return 'text-on-surface-variant'
  return 'text-on-surface-variant'
}

onMounted(() => store.load())
</script>
