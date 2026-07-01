<template>
  <div class="page h-full flex flex-col">
    <div class="sticky top-0 z-10 bg-background">
      <div class="max-w-2xl mx-auto px-5 pt-5 pb-2">
        <div class="flex items-center justify-between">
          <h1 class="text-xl font-semibold text-on-surface">Games</h1>
          <button @click="saveAndApply" :disabled="store.saving"
            class="px-3 h-9 rounded-full bg-primary text-on-primary text-sm font-medium flex items-center gap-1.5
                   disabled:opacity-50 active:opacity-80 transition-opacity">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M8 5v14l11-7z"/></svg>
            Apply
          </button>
        </div>
        <div class="mt-3 bg-surface-container rounded-full flex items-center gap-2 px-3 py-2">
          <SearchIcon class="text-on-surface-variant shrink-0" />
          <input v-model="store.searchQ" placeholder="Search package…"
            class="bg-transparent border-none outline-none text-sm text-on-surface placeholder-on-surface-variant flex-1" />
          <button v-if="store.searchQ" @click="store.searchQ = ''" class="text-on-surface-variant">
            <CloseIcon />
          </button>
        </div>
      </div>
    </div>

    <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-auto">
      <div class="max-w-2xl mx-auto px-5 py-2">
        <LoadingSpinner v-if="store.loading" />
        <div v-else class="space-y-1.5">
          <div v-for="(g, i) in store.filtered" :key="i" class="md3-list">
            <Ripple @click="openEdit(i)" class="md3-list-item w-full">
              <div class="flex items-center gap-3 px-4 py-3">
                <ToggleSwitch :modelValue="g.enabled"
                  @update:modelValue.stop="store.toggleGame(i)"
                  @click.stop class="shrink-0" />
                <div class="flex-1 min-w-0">
                  <p class="text-sm font-medium text-on-surface truncate">{{ g.note || g.package }}</p>
                  <p class="text-xs text-on-surface-variant truncate">{{ g.package }}</p>
                  <div class="flex gap-2 mt-1 flex-wrap">
                    <span class="text-[10px] px-1.5 py-0.5 rounded-sm bg-primary/15 text-primary font-semibold">
                      ×{{ g.downscale }}
                    </span>
                    <span v-if="g.cleanup_logs" class="text-[10px] px-1.5 py-0.5 rounded-sm bg-surface-container-high text-on-surface-variant font-semibold">
                      LOG CLEAN
                    </span>
                  </div>
                </div>
                <ChevronRightIcon class="text-on-surface-variant shrink-0" />
              </div>
            </Ripple>
          </div>
          <div v-if="store.filtered.length === 0 && !store.loading"
            class="text-center py-8 text-on-surface-variant text-sm">No games configured</div>
        </div>
      </div>
    </div>

    <!-- FAB -->
    <button @click="openAdd"
      class="fixed bottom-24 right-5 w-14 h-14 bg-primary rounded-2xl flex items-center justify-center
             text-on-primary shadow-lg active:opacity-80 transition-opacity z-10">
      <PlusIcon />
    </button>

    <!-- Edit/Add modal -->
    <Modal :show="editModal" :title="editIdx === null ? 'Add game' : 'Edit game'" @close="editModal = false">
      <div class="px-4 py-2 space-y-3">
        <InputField v-model="form.package" label="Package name" placeholder="com.example.game" />
        <InputField v-model="form.note"    label="Display name (optional)" placeholder="My Game" />
        <div>
          <label class="block text-xs text-on-surface-variant mb-1">Downscale (0.1 – 1.0)</label>
          <div class="flex items-center gap-3">
            <input type="range" min="0.1" max="1.0" step="0.05" v-model.number="form.downscale"
              class="flex-1 accent-primary" />
            <span class="text-sm font-mono text-on-surface w-10 text-right">{{ form.downscale }}</span>
          </div>
        </div>
        <div class="flex items-center justify-between bg-surface-container-high rounded-xl px-3 py-2.5">
          <div>
            <p class="text-sm text-on-surface">Cleanup logs</p>
            <p class="text-xs text-on-surface-variant">Purge log dirs on apply</p>
          </div>
          <ToggleSwitch v-model="form.cleanup_logs" />
        </div>
        <div v-if="form.cleanup_logs">
          <label class="block text-xs text-on-surface-variant mb-1">Log dirs (one per line)</label>
          <textarea v-model="form.logDirsRaw"
            placeholder="/data/media/0/Android/data/com.example.game/files/logs"
            rows="3"
            class="w-full bg-surface-container-high rounded-xl px-3 py-2.5 text-xs
                   text-on-surface font-mono placeholder-on-surface-variant/50
                   border border-transparent focus:border-primary outline-none resize-none" />
        </div>
      </div>
      <template #actions>
        <button v-if="editIdx !== null" @click="removeAndClose"
          class="px-4 py-2 text-sm font-medium text-error mr-auto">Delete</button>
        <button @click="editModal = false" class="px-4 py-2 text-sm font-medium text-on-surface-variant">Cancel</button>
        <button @click="confirmEdit" class="px-4 py-2 text-sm font-medium text-primary">
          {{ editIdx === null ? 'Add' : 'Save' }}
        </button>
      </template>
    </Modal>

    <Snackbar :show="!!toast" :message="toast" />
  </div>
</template>

<script setup>
import { ref, reactive, onMounted } from 'vue'
import { useGamesStore } from '@/stores/games'
import Ripple        from '@/components/ui/Ripple.vue'
import ToggleSwitch  from '@/components/ui/ToggleSwitch.vue'
import Modal         from '@/components/ui/Modal.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'
import Snackbar      from '@/components/ui/Snackbar.vue'
import InputField    from '@/components/ui/InputField.vue'
import SearchIcon    from '@/components/icons/Search.vue'
import CloseIcon     from '@/components/icons/Close.vue'
import PlusIcon      from '@/components/icons/Plus.vue'
import ChevronRightIcon from '@/components/icons/ChevronRight.vue'

const store     = useGamesStore()
const editModal = ref(false)
const editIdx   = ref(null)
const toast     = ref('')
const form      = reactive({ package: '', note: '', downscale: 0.7, cleanup_logs: false, logDirsRaw: '' })

function showToast(msg) { toast.value = msg; setTimeout(() => (toast.value = ''), 2500) }
onMounted(() => store.load())

function openAdd() {
  editIdx.value = null
  Object.assign(form, { package: '', note: '', downscale: 0.7, cleanup_logs: false, logDirsRaw: '' })
  editModal.value = true
}

function openEdit(i) {
  const g = store.games[i]
  editIdx.value = i
  Object.assign(form, {
    package: g.package, note: g.note ?? '', downscale: g.downscale ?? 0.7,
    cleanup_logs: g.cleanup_logs ?? false,
    logDirsRaw: (g.log_dirs ?? []).join('\n'),
  })
  editModal.value = true
}

function confirmEdit() {
  const log_dirs = form.logDirsRaw.split('\n').map(s => s.trim()).filter(Boolean)
  const data = { package: form.package, note: form.note, enabled: true,
    downscale: form.downscale, cleanup_logs: form.cleanup_logs, log_dirs }
  if (editIdx.value === null) {
    store.addGame(data)
  } else {
    Object.assign(store.games[editIdx.value], data)
  }
  editModal.value = false
}

function removeAndClose() {
  store.removeGame(editIdx.value)
  editModal.value = false
}

async function saveAndApply() {
  const { errno, stderr } = await store.saveAndApply()
  showToast(errno === 0 ? 'Game config applied ✓' : `Error: ${stderr}`)
}
</script>
