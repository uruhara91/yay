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
        <div v-else-if="store.error" class="text-error text-sm py-4 text-center">{{ store.error }}</div>
        <div v-else class="space-y-1.5">
          <div v-for="g in store.filtered" :key="g.package + g.note" class="md3-list">
            <Ripple @click="openEdit(store.indexOf(g))" class="md3-list-item w-full">
              <div class="flex items-center gap-3 px-4 py-3">
                <ToggleSwitch :modelValue="g.enabled"
                  @update:modelValue="store.toggleGame(store.indexOf(g))"
                  @click.stop
                  @pointerdown.stop class="shrink-0" />
                <div class="flex-1 min-w-0">
                  <p class="text-sm font-medium text-on-surface truncate">{{ g.note || g.package }}</p>
                  <p class="text-xs text-on-surface-variant truncate">{{ g.package }}</p>
                  <div class="flex gap-2 mt-1 flex-wrap">
                    <span class="text-[10px] px-1.5 py-0.5 rounded-sm font-semibold"
                      :class="downscaleBadgeClass(g.downscale)">
                      {{ downscaleBadgeText(g.downscale) }}
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
          <label class="block text-xs text-on-surface-variant mb-1">Downscale</label>
          <div class="grid grid-cols-3 gap-2">
            <button v-for="opt in downscaleOptions" :key="opt.value"
              @click="form.downscale = opt.value"
              class="py-2 rounded-xl text-sm font-medium border transition-colors"
              :class="form.downscale === opt.value
                ? (opt.value === 'disable' ? 'bg-error/15 border-error text-error' : 'bg-primary/15 border-primary text-primary')
                : 'border-outline-variant text-on-surface-variant'">
              {{ opt.label }}
            </button>
          </div>
          <p class="text-[11px] text-on-surface-variant mt-1.5">
            Only values Android's <code class="font-mono">cmd game downscale</code> shell command
            actually recognizes — anything else is silently ignored system-side.
          </p>
          <p v-if="isOffGridDownscale" class="text-[11px] text-error mt-1">
            ⚠ Current value ({{ form.downscale }}) isn't one of the recognized options above and
            was never actually being applied — pick one to fix it.
          </p>
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
          <p class="text-[11px] text-on-surface-variant mt-1">
            Must be under <code class="font-mono">Android/data/&lt;package&gt;/…</code> or
            <code class="font-mono">Android/obb/&lt;package&gt;/…</code> — anything else is silently
            rejected by the backend for safety.
          </p>
          <p v-if="unsafeLogDirs.length" class="text-[11px] text-error mt-1">
            ⚠ Won't be purged (outside allowed scope): {{ unsafeLogDirs.join(', ') }}
          </p>
        </div>
      </div>
      <template #actions>
        <button v-if="editIdx !== null" @click="removeAndClose"
          class="px-4 py-2 text-sm font-medium text-error mr-auto">Delete</button>
        <button @click="editModal = false" class="px-4 py-2 text-sm font-medium text-on-surface-variant">Cancel</button>
        <button @click="confirmEdit" :disabled="!form.package.trim()"
          class="px-4 py-2 text-sm font-medium text-primary disabled:opacity-40">
          {{ editIdx === null ? 'Add' : 'Save' }}
        </button>
      </template>
    </Modal>

    <Snackbar :show="!!toast" :message="toast" />
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted } from 'vue'
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

// Exactly what `cmd game downscale` (the legacy GameManager shell command
// this backend uses) recognizes — see AOSP's GameManagerShellCommand.java.
// Any other value is silently ignored by Android, so the picker only ever
// offers these six.
const downscaleOptions = [
  { value: 0.5, label: '0.5' },
  { value: 0.6, label: '0.6' },
  { value: 0.7, label: '0.7' },
  { value: 0.8, label: '0.8' },
  { value: 0.9, label: '0.9' },
  { value: 'disable', label: 'Disable' },
]
const VALID_DOWNSCALE_VALUES = new Set([0.5, 0.6, 0.7, 0.8, 0.9])

// Mirrors game_mode.cpp's is_safe_purge_target(): path must sit under one of
// the two allowed roots, scoped to the game's own package, no ".." traversal,
// and at least one path segment past the package name.
const ALLOWED_ROOTS = ['/data/media/0/Android/data/', '/data/media/0/Android/obb/']
function isSafePurgeTarget(path, pkg) {
  if (!path || path[0] !== '/' || path.includes('..')) return false
  const root = ALLOWED_ROOTS.find((r) => path.startsWith(r))
  if (!root) return false
  const remainder = path.slice(root.length)
  if (!pkg || !remainder.startsWith(`${pkg}/`)) return false
  return remainder.length > pkg.length + 1
}

const isOffGridDownscale = computed(() => {
  const d = form.downscale
  if (d === 'disable' || d === undefined || d === null || d === '') return false
  return !VALID_DOWNSCALE_VALUES.has(d)
})

const unsafeLogDirs = computed(() => {
  if (!form.cleanup_logs) return []
  return form.logDirsRaw
    .split('\n')
    .map((s) => s.trim())
    .filter(Boolean)
    .filter((p) => !isSafePurgeTarget(p, form.package.trim()))
})

function downscaleBadgeText(d) {
  if (d === 'disable') return 'downscale off'
  if (typeof d === 'number' && VALID_DOWNSCALE_VALUES.has(d)) return `×${d}`
  if (d === undefined || d === null) return 'not set'
  return `×${d} ⚠`
}

function downscaleBadgeClass(d) {
  if (d === 'disable') return 'bg-surface-container-high text-on-surface-variant'
  if (typeof d === 'number' && VALID_DOWNSCALE_VALUES.has(d)) return 'bg-primary/15 text-primary'
  if (d === undefined || d === null) return 'bg-surface-container-high text-on-surface-variant'
  // Off-grid value (e.g. 0.65 from before this validation existed, or a
  // hand-edited config file) — Android silently ignores it, so flag it
  // rather than implying it's actually in effect.
  return 'bg-error/15 text-error'
}

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
  if (!form.package.trim()) return
  const log_dirs = form.logDirsRaw.split('\n').map(s => s.trim()).filter(Boolean)
  const data = { package: form.package.trim(), note: form.note, enabled: true,
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
