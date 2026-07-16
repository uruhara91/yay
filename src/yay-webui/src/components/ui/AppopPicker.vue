<template>
  <Modal :show="show" title="Browse appops" @close="$emit('close')">
    <div class="px-4 pb-2 space-y-3">
      <InputField v-model="pkg" label="Package (optional — flags ops it has used)" placeholder="com.example.app" @keyup.enter="load" />
      <button @click="load" :disabled="loading"
        class="w-full py-2 rounded-xl text-sm font-medium bg-primary/15 text-primary
               disabled:opacity-40 transition-opacity">
        {{ loading ? 'Loading…' : (loaded ? 'Refresh' : 'Load appops list') }}
      </button>

      <LoadingSpinner v-if="loading" :size="28" />

      <p v-else-if="error" class="text-error text-sm text-center py-4">{{ error }}</p>

      <template v-else-if="loaded">
        <div class="bg-surface-container-high rounded-full flex items-center gap-2 px-3 py-2">
          <SearchIcon class="text-on-surface-variant shrink-0" />
          <input v-model="search" placeholder="Filter by name…"
            class="bg-transparent border-none outline-none text-sm text-on-surface placeholder-on-surface-variant flex-1" />
        </div>

        <div class="flex flex-wrap gap-2">
          <button v-for="cat in categories" :key="cat"
            @click="activeCategory = activeCategory === cat ? null : cat"
            class="px-3 py-1.5 rounded-full text-xs font-medium border transition-colors"
            :class="activeCategory === cat
              ? 'bg-primary/15 border-primary text-primary'
              : 'border-outline-variant text-on-surface-variant'">
            {{ cat }}
          </button>
        </div>

        <div class="max-h-80 overflow-y-auto scrollbar-hidden space-y-1.5 -mx-1 px-1">
          <button v-for="op in filtered" :key="op.op"
            @click="$emit('pick', op)"
            class="w-full text-left bg-surface-container-high rounded-xl px-3 py-2.5
                   active:bg-surface-container-highest transition-colors">
            <div class="flex items-start justify-between gap-2">
              <div class="min-w-0">
                <p class="text-sm font-medium text-on-surface">{{ op.label }}</p>
                <p class="text-[11px] font-mono text-on-surface-variant break-all mt-0.5">{{ op.op }}</p>
              </div>
              <span v-if="pkg.trim() && op.active"
                class="shrink-0 text-[10px] px-1.5 py-0.5 rounded-sm font-semibold uppercase bg-primary/15 text-primary">
                used
              </span>
            </div>
          </button>
          <p v-if="filtered.length === 0" class="text-center py-6 text-on-surface-variant text-sm">
            No matching ops
          </p>
        </div>
      </template>

      <p v-else class="text-xs text-on-surface-variant text-center py-2">
        Built-in list of known Android app-ops. Give a package name to see
        which ones it has actually used; leave it blank to just browse.
      </p>
    </div>
    <template #actions>
      <button @click="$emit('close')" class="px-4 py-2 text-sm font-medium text-on-surface-variant">Close</button>
    </template>
  </Modal>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { inspectAppops } from '@/helpers/bridge'
import Modal from '@/components/ui/Modal.vue'
import InputField from '@/components/ui/InputField.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'
import SearchIcon from '@/components/icons/Search.vue'

const props = defineProps({
  show: Boolean,
  initialPackage: { type: String, default: '' },
})
const emit = defineEmits(['close', 'pick'])

const pkg = ref('')
const loading = ref(false)
const loaded = ref(false)
const error = ref('')
const ops = ref([])
const search = ref('')
const activeCategory = ref(null)

const categories = computed(() => [...new Set(ops.value.map((o) => o.category))].sort())

const filtered = computed(() => {
  let list = ops.value
  if (activeCategory.value) list = list.filter((o) => o.category === activeCategory.value)
  if (search.value.trim()) {
    const q = search.value.trim().toLowerCase()
    list = list.filter(
      (o) => o.label.toLowerCase().includes(q) || o.op.toLowerCase().includes(q),
    )
  }
  // Ops the package has actually used float to the top — that's almost
  // always what the person is looking for when they gave a package name.
  return [...list].sort((a, b) => (b.active ? 1 : 0) - (a.active ? 1 : 0))
})

watch(
  () => props.show,
  (show) => {
    if (show) {
      pkg.value = props.initialPackage ?? ''
      loaded.value = false
      error.value = ''
      ops.value = []
      search.value = ''
      activeCategory.value = null
      load()
    }
  },
)

async function load() {
  loading.value = true
  error.value = ''
  try {
    ops.value = await inspectAppops(pkg.value.trim())
    loaded.value = true
  } catch (e) {
    error.value = e.message
  } finally {
    loading.value = false
  }
}
</script>
