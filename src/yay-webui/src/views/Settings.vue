<template>
  <div class="page h-full flex flex-col">
    <div class="sticky top-0 z-10 bg-background">
      <div class="max-w-2xl mx-auto px-5 pt-5 pb-3">
        <h1 class="text-xl font-semibold text-on-surface">Settings</h1>
      </div>
    </div>

    <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-auto">
      <div class="max-w-2xl mx-auto px-5 py-2 space-y-4">

        <!-- Watcher -->
        <div class="px-1 pb-1">
          <h2 class="text-xs font-medium text-on-surface-variant uppercase tracking-wide">Automation</h2>
        </div>
        <div class="bg-surface-container rounded-2xl">
          <div class="flex items-center justify-between px-4 py-4 gap-4">
            <div class="min-w-0 flex-1">
              <p class="text-sm font-medium text-on-surface">Inotify watcher</p>
              <p class="text-xs text-on-surface-variant mt-1">
                Re-applies rules/game config automatically the moment a JSON file changes.
                Takes effect on next boot.
              </p>
            </div>
            <LoadingSpinner v-if="watcherLoading" :size="20" class="shrink-0" />
            <ToggleSwitch v-else :modelValue="watcherEnabled" @update:modelValue="toggleWatcher" class="shrink-0" />
          </div>
        </div>

        <!-- Log export -->
        <div class="px-1 pb-1 pt-2">
          <h2 class="text-xs font-medium text-on-surface-variant uppercase tracking-wide">Diagnostics</h2>
        </div>
        <div class="bg-surface-container rounded-2xl overflow-hidden">
          <Ripple @click="openExportModal" class="w-full">
            <div class="flex items-center gap-4 px-4 py-4">
              <div class="w-9 h-9 rounded-full bg-primary-container flex items-center justify-center shrink-0">
                <ContentSaveIcon class="text-on-primary-container" />
              </div>
              <div class="flex-1 min-w-0">
                <p class="text-sm font-medium text-on-surface">Export logs</p>
                <p class="text-xs text-on-surface-variant mt-0.5">Bundle run.log + configs to /sdcard</p>
              </div>
              <ChevronRightIcon class="text-on-surface-variant shrink-0" />
            </div>
          </Ripple>
        </div>

        <!-- About -->
        <div class="px-1 pb-1 pt-2">
          <h2 class="text-xs font-medium text-on-surface-variant uppercase tracking-wide">About</h2>
        </div>
        <div class="bg-surface-container rounded-2xl divide-y divide-outline-variant/15">
          <InfoRow icon="🧩" label="Version" value="yay v1.1" />
          <InfoRow icon="🔗" label="Bridge" :value="bridge" />
          <InfoRow icon="📁" label="Config path" value="/data/adb/yay/config/" mono />
          <InfoRow icon="📜" label="Log path" value="/data/adb/yay/run.log" mono />
        </div>

      </div>
    </div>

    <!-- Export modal -->
    <Modal :show="showExportModal" title="Export logs" @close="closeExportModal" :closeOnOutsideClick="false">
      <div class="px-4 pb-2">
        <div v-if="exportStatus === 'loading'" class="flex flex-col items-center gap-4 py-6">
          <LoadingSpinner :size="40" />
          <p class="text-on-surface-variant text-sm">Exporting…</p>
        </div>

        <div v-else-if="exportStatus === 'success'" class="flex flex-col items-center gap-3 py-4">
          <CheckIcon class="text-primary" />
          <p class="text-on-surface font-medium text-center">Exported successfully</p>
          <p class="text-on-surface-variant text-xs break-all text-center bg-surface-container-low px-4 py-3 rounded-xl w-full">
            {{ exportPath }}
          </p>
        </div>

        <div v-else-if="exportStatus === 'error'" class="flex flex-col items-center gap-3 py-4">
          <ErrorIcon class="text-error" />
          <p class="text-on-surface font-medium text-center">Export failed</p>
          <p class="text-on-surface-variant text-sm text-center">{{ exportErrorMsg }}</p>
        </div>
      </div>

      <template #actions>
        <div v-if="exportStatus !== 'loading'" class="flex gap-2">
          <button @click="closeExportModal" class="px-4 py-2 text-sm font-medium text-primary rounded-full hover:bg-primary/10 transition-colors">
            {{ exportStatus === 'success' ? 'Close' : 'OK' }}
          </button>
        </div>
      </template>
    </Modal>

    <Snackbar :show="!!toast" :message="toast" />
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import {
  isWatcherEnabled,
  setWatcherEnabled,
  bridgeName,
  exec,
  PATHS,
} from '@/helpers/bridge'

import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'
import Ripple from '@/components/ui/Ripple.vue'
import Modal from '@/components/ui/Modal.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'
import Snackbar from '@/components/ui/Snackbar.vue'
import InfoRow from '@/components/ui/InfoRow.vue'
import ContentSaveIcon from '@/components/icons/ContentSave.vue'
import ChevronRightIcon from '@/components/icons/ChevronRight.vue'
import CheckIcon from '@/components/icons/Check.vue'
import ErrorIcon from '@/components/icons/Error.vue'

const watcherEnabled = ref(false)
const watcherLoading = ref(true)
const bridge = ref(bridgeName())
const toast = ref('')

const showExportModal = ref(false)
const exportStatus = ref('idle') // idle | loading | success | error
const exportPath = ref('')
const exportErrorMsg = ref('')

function showToast(msg) {
  toast.value = msg
  setTimeout(() => (toast.value = ''), 2500)
}

onMounted(async () => {
  watcherLoading.value = true
  try {
    watcherEnabled.value = await isWatcherEnabled()
  } finally {
    watcherLoading.value = false
  }
})

async function toggleWatcher(value) {
  watcherEnabled.value = value
  try {
    await setWatcherEnabled(value)
    showToast(value ? 'Watcher enabled — takes effect next boot' : 'Watcher disabled')
  } catch (e) {
    watcherEnabled.value = !value
    showToast(`Error: ${e.message}`)
  }
}

function openExportModal() {
  exportStatus.value = 'loading'
  exportPath.value = ''
  exportErrorMsg.value = ''
  showExportModal.value = true

  const dest = `/sdcard/yay_logs_${Date.now()}.tar.gz`
  exec(`tar -czf "${dest}" -C /data/adb/yay run.log config 2>&1`)
    .then(({ errno, stderr }) => {
      if (errno !== 0) {
        exportStatus.value = 'error'
        exportErrorMsg.value = stderr.trim() || 'Unknown error'
      } else {
        exportStatus.value = 'success'
        exportPath.value = dest
      }
    })
    .catch((err) => {
      exportStatus.value = 'error'
      exportErrorMsg.value = err.message
    })
}

function closeExportModal() {
  showExportModal.value = false
  setTimeout(() => {
    if (exportStatus.value !== 'loading') {
      exportStatus.value = 'idle'
      exportPath.value = ''
      exportErrorMsg.value = ''
    }
  }, 200)
}
</script>
