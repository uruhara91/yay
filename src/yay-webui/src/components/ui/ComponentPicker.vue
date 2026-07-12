<template>
  <Modal :show="show" title="Browse components" @close="$emit('close')">
    <div class="px-4 pb-2 space-y-3">
      <InputField v-model="pkg" label="Package" placeholder="com.example.app" @keyup.enter="load" />
      <button @click="load" :disabled="!pkg.trim() || loading"
        class="w-full py-2 rounded-xl text-sm font-medium bg-primary/15 text-primary
               disabled:opacity-40 transition-opacity">
        {{ loading ? 'Reading manifest…' : 'Load components' }}
      </button>

      <LoadingSpinner v-if="loading" :size="28" />

      <p v-else-if="error" class="text-error text-sm text-center py-4">{{ error }}</p>

      <template v-else-if="loaded">
        <!-- Type filter chips -->
        <div class="flex flex-wrap gap-2">
          <button v-for="t in types" :key="t"
            @click="activeType = activeType === t ? null : t"
            class="px-3 py-1.5 rounded-full text-xs font-medium border transition-colors"
            :class="activeType === t
              ? 'bg-primary/15 border-primary text-primary'
              : 'border-outline-variant text-on-surface-variant'">
            {{ t }} ({{ countByType[t] ?? 0 }})
          </button>
        </div>

        <div class="max-h-80 overflow-y-auto scrollbar-hidden space-y-1.5 -mx-1 px-1">
          <button v-for="c in filtered" :key="c.type + c.name"
            @click="$emit('pick', c)"
            class="w-full text-left bg-surface-container-high rounded-xl px-3 py-2.5
                   active:bg-surface-container-highest transition-colors">
            <div class="flex items-start justify-between gap-2">
              <p class="text-xs font-mono text-on-surface break-all">{{ c.name }}</p>
              <span class="shrink-0 text-[10px] px-1.5 py-0.5 rounded-sm font-semibold uppercase
                           bg-surface-container text-on-surface-variant">
                {{ c.type }}
              </span>
            </div>
            <div class="flex items-center gap-2 mt-1">
              <span class="text-[11px]"
                :class="c.exported ? 'text-primary' : 'text-on-surface-variant'">
                exported={{ c.exported }}{{ c.exported_explicit ? '' : ' (default)' }}
              </span>
              <span v-if="!c.enabled" class="text-[11px] text-error">disabled in manifest</span>
            </div>
          </button>
          <p v-if="filtered.length === 0" class="text-center py-6 text-on-surface-variant text-sm">
            No {{ activeType ?? 'components' }} found
          </p>
        </div>
      </template>

      <p v-else class="text-xs text-on-surface-variant text-center py-2">
        Reads the app's own compiled manifest directly — no dumpsys, no
        terminal. Tap a result to fill it in.
      </p>
    </div>
    <template #actions>
      <button @click="$emit('close')" class="px-4 py-2 text-sm font-medium text-on-surface-variant">Close</button>
    </template>
  </Modal>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { inspectComponents } from '@/helpers/bridge'
import Modal from '@/components/ui/Modal.vue'
import InputField from '@/components/ui/InputField.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'

const props = defineProps({
  show: Boolean,
  initialPackage: { type: String, default: '' },
})
const emit = defineEmits(['close', 'pick'])

const pkg = ref('')
const loading = ref(false)
const loaded = ref(false)
const error = ref('')
const components = ref([])
const activeType = ref(null)

const types = ['activity', 'service', 'receiver', 'provider']

const countByType = computed(() => {
  const counts = {}
  for (const c of components.value) counts[c.type] = (counts[c.type] ?? 0) + 1
  return counts
})

const filtered = computed(() =>
  activeType.value ? components.value.filter((c) => c.type === activeType.value) : components.value,
)

watch(
  () => props.show,
  (show) => {
    if (show) {
      pkg.value = props.initialPackage ?? ''
      loaded.value = false
      error.value = ''
      components.value = []
      activeType.value = null
      if (pkg.value.trim()) load()
    }
  },
)

async function load() {
  if (!pkg.value.trim()) return
  loading.value = true
  error.value = ''
  try {
    components.value = await inspectComponents(pkg.value.trim())
    loaded.value = true
    if (components.value.length === 0) {
      error.value = `No components found for ${pkg.value.trim()} — check the package name is installed and correct.`
    }
  } catch (e) {
    error.value = e.message
    loaded.value = false
  } finally {
    loading.value = false
  }
}
</script>
