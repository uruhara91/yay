<template>
  <div class="page h-full flex flex-col">
    <!-- Header -->
    <div class="sticky top-0 z-10 bg-background">
      <div class="max-w-2xl mx-auto px-5 pt-5 pb-2">
        <div class="flex items-center justify-between">
          <h1 class="text-xl font-semibold text-on-surface">I/O Scheduler</h1>
          <button @click="saveAndApply" :disabled="store.saving"
            class="px-3 h-9 rounded-full bg-primary text-on-primary text-sm font-medium flex items-center gap-1.5
                   disabled:opacity-50 active:opacity-80 transition-opacity">
            <PlayIcon />
            Apply
          </button>
        </div>
        <p class="text-xs text-on-surface-variant mt-2">
          Ordered by preference — the first scheduler available on this device wins per block device.
        </p>
      </div>
    </div>

    <!-- Content -->
    <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-auto">
      <div class="max-w-2xl mx-auto px-5 py-2 space-y-4">
        <LoadingSpinner v-if="store.loading" />

        <div v-else-if="store.error" class="text-error text-sm py-4 text-center">{{ store.error }}</div>

        <template v-else>
          <!-- Scheduler preference list -->
          <div>
            <p class="px-1 pb-2 text-xs font-medium text-on-surface-variant uppercase tracking-wide">
              Scheduler preference
            </p>
            <div class="bg-surface-container rounded-2xl overflow-hidden divide-y divide-outline-variant/15">
              <div v-for="(sched, i) in store.schedulerPreference" :key="sched"
                class="flex items-center gap-3 px-4 py-3">
                <span class="w-6 h-6 rounded-full bg-surface-container-high text-on-surface-variant
                             text-xs font-semibold flex items-center justify-center shrink-0">
                  {{ i + 1 }}
                </span>
                <span class="flex-1 text-sm font-mono text-on-surface">{{ sched }}</span>
                <div class="flex items-center gap-0.5 shrink-0">
                  <button @click="store.moveUp(i)" :disabled="i === 0"
                    class="p-1.5 rounded-full text-on-surface-variant disabled:opacity-30 active:bg-on-surface/10 transition-colors">
                    <ArrowUpIcon />
                  </button>
                  <button @click="store.moveDown(i)" :disabled="i === store.schedulerPreference.length - 1"
                    class="p-1.5 rounded-full text-on-surface-variant disabled:opacity-30 active:bg-on-surface/10 transition-colors">
                    <ArrowDownIcon />
                  </button>
                  <button @click="store.removeScheduler(i)"
                    class="p-1.5 rounded-full text-on-surface-variant hover:text-error active:bg-error/10 transition-colors">
                    <TrashIcon />
                  </button>
                </div>
              </div>
              <div v-if="store.schedulerPreference.length === 0" class="text-center py-6 text-on-surface-variant text-sm">
                No schedulers configured
              </div>
            </div>

            <!-- Add scheduler -->
            <div class="mt-2 bg-surface-container rounded-2xl p-2 flex flex-wrap gap-2">
              <button v-for="s in availableSchedulers" :key="s" @click="store.addScheduler(s)"
                class="px-3 py-1.5 rounded-full text-xs font-medium border border-outline-variant
                       text-on-surface-variant active:bg-on-surface/10 transition-colors">
                + {{ s }}
              </button>
            </div>
          </div>

          <!-- Read-ahead -->
          <div class="bg-surface-container rounded-2xl p-4">
            <label class="block text-xs font-medium text-on-surface-variant uppercase tracking-wide mb-2">
              Read-ahead (KB)
            </label>
            <div class="flex items-center gap-3">
              <input type="range" min="0" max="512" step="16" v-model.number="store.readAheadKb"
                class="flex-1 accent-primary" />
              <span class="text-sm font-mono text-on-surface w-14 text-right">{{ store.readAheadKb }}</span>
            </div>
            <p class="text-xs text-on-surface-variant mt-2">
              Applied per block device via <code class="font-mono">/sys/block/*/queue/read_ahead_kb</code>.
            </p>
          </div>
        </template>
      </div>
    </div>

    <Snackbar :show="!!toast" :message="toast" />
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useIoStore } from '@/stores/io'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'
import Snackbar from '@/components/ui/Snackbar.vue'
import PlayIcon from '@/components/icons/Play.vue'
import ArrowUpIcon from '@/components/icons/ArrowUp.vue'
import ArrowDownIcon from '@/components/icons/ArrowDown.vue'
import TrashIcon from '@/components/icons/Trash.vue'

const store = useIoStore()
const toast = ref('')

const availableSchedulers = computed(() =>
  store.KNOWN_SCHEDULERS.filter((s) => !store.schedulerPreference.includes(s)),
)

function showToast(msg) {
  toast.value = msg
  setTimeout(() => (toast.value = ''), 2500)
}

onMounted(() => store.load())

async function saveAndApply() {
  const { errno, stderr } = await store.saveAndApply()
  showToast(errno === 0 ? 'I/O config applied ✓' : `Error: ${stderr}`)
}
</script>
