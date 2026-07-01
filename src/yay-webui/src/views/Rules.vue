<template>
  <div class="page h-full flex flex-col">
    <!-- Header -->
    <div class="sticky top-0 z-10 bg-background">
      <div class="max-w-2xl mx-auto px-5 pt-5 pb-2">
        <div class="flex items-center justify-between">
          <h1 class="text-xl font-semibold text-on-surface">Rules</h1>
          <div class="flex gap-2">
            <button @click="showDryRun" title="Preview"
              class="w-9 h-9 rounded-full bg-surface-container flex items-center justify-center text-on-surface-variant active:bg-surface-container-high transition-colors">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8a3 3 0 100 6 3 3 0 000-6z"/></svg>
            </button>
            <button @click="saveAndApply" :disabled="store.saving" title="Save &amp; Apply"
              class="px-3 h-9 rounded-full bg-primary text-on-primary text-sm font-medium flex items-center gap-1.5
                     disabled:opacity-50 active:opacity-80 transition-opacity">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M8 5v14l11-7z"/></svg>
              Apply
            </button>
          </div>
        </div>
        <!-- Tab bar -->
        <div class="flex mt-3 bg-surface-container rounded-xl p-1 gap-1">
          <button v-for="t in tabs" :key="t.key" @click="tab = t.key"
            class="flex-1 py-1.5 rounded-lg text-sm font-medium transition-colors"
            :class="tab === t.key
              ? 'bg-secondary-container text-on-secondary-container'
              : 'text-on-surface-variant'">
            {{ t.label }}
            <span class="ml-1 text-xs opacity-60">({{ t.count.value }})</span>
          </button>
        </div>
        <!-- Search -->
        <div v-if="tab === 'components'" class="mt-2 bg-surface-container rounded-full flex items-center gap-2 px-3 py-2">
          <SearchIcon class="text-on-surface-variant shrink-0" />
          <input v-model="store.searchQ" placeholder="Search package or component…"
            class="bg-transparent border-none outline-none text-sm text-on-surface placeholder-on-surface-variant flex-1" />
          <button v-if="store.searchQ" @click="store.searchQ = ''" class="text-on-surface-variant">
            <CloseIcon />
          </button>
        </div>
      </div>
    </div>

    <!-- Content -->
    <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-auto">
      <div class="max-w-2xl mx-auto px-5 py-2">
        <LoadingSpinner v-if="store.loading" />

        <div v-else-if="store.error" class="text-error text-sm py-4 text-center">{{ store.error }}</div>

        <!-- Components tab -->
        <template v-else-if="tab === 'components'">
          <div class="space-y-1.5">
            <div v-for="(c, i) in store.filteredComponents" :key="i" class="md3-list">
              <div class="flex items-start px-4 py-3 gap-3">
                <ToggleSwitch :modelValue="c.enabled" @update:modelValue="store.toggleComponent(store.components.indexOf(c))" class="mt-0.5 shrink-0" />
                <div class="flex-1 min-w-0">
                  <p class="text-sm font-medium text-on-surface truncate">{{ shortName(c.component) }}</p>
                  <p class="text-xs text-on-surface-variant truncate mt-0.5">{{ c.package }}</p>
                  <p v-if="c.note" class="text-xs text-primary mt-0.5">{{ c.note }}</p>
                  <span class="inline-block mt-1 text-[10px] px-1.5 py-0.5 rounded-sm font-semibold uppercase"
                    :class="c.action === 'enable'
                      ? 'bg-primary/15 text-primary'
                      : 'bg-error/15 text-error'">
                    {{ c.action }}
                  </span>
                </div>
                <button @click="store.removeComponent(store.components.indexOf(c))"
                  class="p-1.5 rounded-full text-on-surface-variant hover:text-error transition-colors shrink-0 mt-0.5">
                  <TrashIcon />
                </button>
              </div>
            </div>
            <div v-if="store.filteredComponents.length === 0" class="text-center py-8 text-on-surface-variant text-sm">
              No components found
            </div>
          </div>
          <!-- FAB -->
          <button @click="openAddComponent"
            class="fixed bottom-24 right-5 w-14 h-14 bg-primary rounded-2xl flex items-center justify-center
                   text-on-primary shadow-lg active:opacity-80 transition-opacity z-10">
            <PlusIcon />
          </button>
        </template>

        <!-- Appops tab -->
        <template v-else>
          <div class="space-y-1.5">
            <div v-for="(a, i) in store.appops" :key="i" class="md3-list">
              <div class="flex items-start px-4 py-3 gap-3">
                <ToggleSwitch :modelValue="a.enabled" @update:modelValue="store.toggleAppop(i)" class="mt-0.5 shrink-0" />
                <div class="flex-1 min-w-0">
                  <p class="text-sm font-medium text-on-surface">{{ a.package }}</p>
                  <p class="text-xs text-on-surface-variant mt-0.5">op <span class="font-mono">{{ a.op }}</span> → <span class="font-semibold">{{ a.mode }}</span></p>
                  <p v-if="a.note" class="text-xs text-primary mt-0.5">{{ a.note }}</p>
                </div>
                <button @click="store.removeAppop(i)"
                  class="p-1.5 rounded-full text-on-surface-variant hover:text-error transition-colors shrink-0 mt-0.5">
                  <TrashIcon />
                </button>
              </div>
            </div>
            <div v-if="store.appops.length === 0" class="text-center py-8 text-on-surface-variant text-sm">
              No appops overrides
            </div>
          </div>
          <button @click="openAddAppop"
            class="fixed bottom-24 right-5 w-14 h-14 bg-primary rounded-2xl flex items-center justify-center
                   text-on-primary shadow-lg active:opacity-80 transition-opacity z-10">
            <PlusIcon />
          </button>
        </template>
      </div>
    </div>

    <!-- Add Component modal -->
    <Modal :show="addCompModal" title="Add component rule" @close="addCompModal = false">
      <div class="px-4 py-2 space-y-3">
        <InputField v-model="newComp.package"   label="Package"   placeholder="com.example.app" />
        <InputField v-model="newComp.component" label="Component" placeholder="com.example.app.SomeService" />
        <InputField v-model="newComp.note"      label="Note (optional)" placeholder="" />
        <div class="flex gap-2">
          <button @click="newComp.action = 'disable'"
            class="flex-1 py-2 rounded-xl text-sm font-medium border transition-colors"
            :class="newComp.action === 'disable'
              ? 'bg-error/15 border-error text-error'
              : 'border-outline-variant text-on-surface-variant'">disable</button>
          <button @click="newComp.action = 'enable'"
            class="flex-1 py-2 rounded-xl text-sm font-medium border transition-colors"
            :class="newComp.action === 'enable'
              ? 'bg-primary/15 border-primary text-primary'
              : 'border-outline-variant text-on-surface-variant'">enable</button>
        </div>
      </div>
      <template #actions>
        <button @click="addCompModal = false" class="px-4 py-2 text-sm font-medium text-on-surface-variant">Cancel</button>
        <button @click="confirmAddComp" class="px-4 py-2 text-sm font-medium text-primary">Add</button>
      </template>
    </Modal>

    <!-- Add Appop modal -->
    <Modal :show="addAppopModal" title="Add appops override" @close="addAppopModal = false">
      <div class="px-4 py-2 space-y-3">
        <InputField v-model="newAppop.package" label="Package" placeholder="com.example.app" />
        <InputField v-model="newAppop.op"      label="Op (integer or name)" placeholder="40" />
        <InputField v-model="newAppop.note"    label="Note (optional)" placeholder="op 40 = GET_USAGE_STATS" />
        <div class="grid grid-cols-2 gap-2">
          <button v-for="m in ['ignore','deny','allow','default']" :key="m"
            @click="newAppop.mode = m"
            class="py-2 rounded-xl text-sm font-medium border transition-colors"
            :class="newAppop.mode === m
              ? 'bg-primary/15 border-primary text-primary'
              : 'border-outline-variant text-on-surface-variant'">{{ m }}</button>
        </div>
      </div>
      <template #actions>
        <button @click="addAppopModal = false" class="px-4 py-2 text-sm font-medium text-on-surface-variant">Cancel</button>
        <button @click="confirmAddAppop" class="px-4 py-2 text-sm font-medium text-primary">Add</button>
      </template>
    </Modal>

    <!-- Dry-run modal -->
    <Modal :show="dryRunModal" title="Dry-run preview" @close="dryRunModal = false">
      <div class="px-4 py-2">
        <LoadingSpinner v-if="dryRunLoading" :size="32" />
        <pre v-else class="text-xs text-on-surface font-mono whitespace-pre-wrap break-all max-h-64 overflow-y-auto">{{ dryRunOutput }}</pre>
      </div>
      <template #actions>
        <button @click="dryRunModal = false" class="px-4 py-2 text-sm font-medium text-primary">Close</button>
      </template>
    </Modal>

    <Snackbar :show="!!toast" :message="toast" />
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRulesStore } from '@/stores/rules'
import ToggleSwitch   from '@/components/ui/ToggleSwitch.vue'
import Modal          from '@/components/ui/Modal.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'
import Snackbar       from '@/components/ui/Snackbar.vue'
import SearchIcon     from '@/components/icons/Search.vue'
import CloseIcon      from '@/components/icons/Close.vue'
import PlusIcon       from '@/components/icons/Plus.vue'
import TrashIcon      from '@/components/icons/Trash.vue'
import InputField     from '@/components/ui/InputField.vue'

const store        = useRulesStore()
const tab          = ref('components')
const toast        = ref('')
const addCompModal   = ref(false)
const addAppopModal  = ref(false)
const dryRunModal    = ref(false)
const dryRunLoading  = ref(false)
const dryRunOutput   = ref('')

const newComp  = ref({ package: '', component: '', action: 'disable', note: '' })
const newAppop = ref({ package: '', op: '', mode: 'ignore', note: '' })

const tabs = [
  { key: 'components', label: 'Components', count: computed(() => store.components.length) },
  { key: 'appops',     label: 'Appops',     count: computed(() => store.appops.length)     },
]

const shortName = (s) => s.split('.').slice(-2).join('.')

function showToast(msg, ms = 2500) {
  toast.value = msg
  setTimeout(() => (toast.value = ''), ms)
}

onMounted(() => store.load())

async function saveAndApply() {
  const { errno, stderr } = await store.saveAndApply()
  showToast(errno === 0 ? 'Rules applied ✓' : `Error: ${stderr}`)
}

function openAddComponent() {
  newComp.value = { package: '', component: '', action: 'disable', note: '' }
  addCompModal.value = true
}
function confirmAddComp() {
  if (!newComp.value.package || !newComp.value.component) return
  store.addComponent(newComp.value)
  addCompModal.value = false
}

function openAddAppop() {
  newAppop.value = { package: '', op: '', mode: 'ignore', note: '' }
  addAppopModal.value = true
}
function confirmAddAppop() {
  if (!newAppop.value.package || !newAppop.value.op) return
  store.addAppop(newAppop.value)
  addAppopModal.value = false
}

async function showDryRun() {
  dryRunModal.value   = true
  dryRunLoading.value = true
  dryRunOutput.value  = ''
  try {
    dryRunOutput.value = await store.previewDryRun()
  } finally {
    dryRunLoading.value = false
  }
}
</script>
