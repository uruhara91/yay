<template>
  <div class="page h-full flex flex-col">
    <div class="sticky top-0 z-10 bg-background">
      <div class="max-w-2xl mx-auto px-5 pt-5 pb-3 flex items-center justify-between">
        <h1 class="text-xl font-semibold text-on-surface">yay</h1>
        <button @click="$router.push('/settings')"
          class="w-9 h-9 rounded-full bg-surface-container flex items-center justify-center
                 text-on-surface-variant active:bg-surface-container-high transition-colors">
          <GearIcon />
        </button>
      </div>
    </div>

    <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-auto">
      <div class="max-w-2xl mx-auto px-5 py-2 space-y-3">

        <!-- Status card -->
        <div class="bg-secondary-container rounded-2xl p-4 flex items-center gap-4">
          <div class="w-12 h-12 rounded-full bg-primary/20 flex items-center justify-center shrink-0">
            <svg width="28" height="28" viewBox="0 0 24 24" fill="currentColor" class="text-primary">
              <path d="M12 1L3 5v6c0 5.55 3.84 10.74 9 12 5.16-1.26 9-6.45 9-12V5l-9-4z"/>
            </svg>
          </div>
          <div class="min-w-0 flex-1">
            <p class="text-sm font-semibold text-on-secondary-container">yay v{{ store.version }}</p>
            <p class="text-xs text-on-secondary-container/70 mt-0.5">
              System tweaks · No daemon · {{ store.bridge }}
            </p>
          </div>
          <span v-if="store.watcherEnabled"
            class="shrink-0 text-[10px] font-semibold uppercase px-2 py-1 rounded-full bg-primary/20 text-on-secondary-container">
            Watching
          </span>
        </div>

        <!-- Info card -->
        <LoadingSpinner v-if="store.loading" :size="28" />
        <div v-else class="bg-surface-container rounded-2xl divide-y divide-outline-variant/20">
          <InfoRow icon="⚙️" label="I/O Scheduler" :value="ioInfoText" />
          <InfoRow icon="📦" label="Rules" :value="`${store.componentCount} components · ${store.appopCount} appops`" />
          <InfoRow icon="🎮" label="Game mode" :value="`${store.gameCount} games configured`" />
          <InfoRow icon="🔧" label="Config path" value="/data/adb/yay/config/" mono />
        </div>

        <!-- Quick actions -->
        <div class="bg-surface-container rounded-2xl overflow-hidden">
          <div class="px-4 py-3">
            <p class="text-xs font-medium text-on-surface-variant uppercase tracking-wide">Quick actions</p>
          </div>
          <Ripple @click="runFull" class="md3-list-item w-full">
            <div class="flex items-center gap-4 px-5 py-4">
              <div class="w-9 h-9 rounded-full bg-primary-container flex items-center justify-center shrink-0">
                <RefreshIcon class="text-on-primary-container" />
              </div>
              <div>
                <p class="text-sm font-medium text-on-surface">Force full apply</p>
                <p class="text-xs text-on-surface-variant mt-0.5">Re-apply all: IO, rules, game mode</p>
              </div>
            </div>
          </Ripple>
          <Ripple @click="$router.push('/log')" class="md3-list-item w-full">
            <div class="flex items-center gap-4 px-5 py-4">
              <div class="w-9 h-9 rounded-full bg-primary-container flex items-center justify-center shrink-0">
                <LogIcon class="text-on-primary-container" :active="true" />
              </div>
              <div>
                <p class="text-sm font-medium text-on-surface">View run log</p>
                <p class="text-xs text-on-surface-variant mt-0.5">/data/adb/yay/run.log</p>
              </div>
            </div>
          </Ripple>
        </div>

      </div>
    </div>

    <!-- Applying overlay -->
    <Transition name="fade">
      <div v-if="applying"
        class="fixed inset-0 z-50 bg-black/50 flex items-center justify-center">
        <div class="bg-surface rounded-2xl p-6 flex flex-col items-center gap-3 mx-6">
          <LoadingSpinner :size="40" />
          <p class="text-sm text-on-surface">Applying…</p>
        </div>
      </div>
    </Transition>

    <Snackbar :show="!!toast" :message="toast" />
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useHomeStore } from '@/stores/home'
import { applyFull } from '@/helpers/bridge'
import Ripple from '@/components/ui/Ripple.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'
import Snackbar from '@/components/ui/Snackbar.vue'
import InfoRow from '@/components/ui/InfoRow.vue'
import GearIcon from '@/components/icons/Gear.vue'
import RefreshIcon from '@/components/icons/Refresh.vue'
import LogIcon from '@/components/icons/Log.vue'

const store = useHomeStore()
const applying = ref(false)
const toast = ref('')

const ioInfoText = computed(() =>
  store.schedulerPreference.length ? store.schedulerPreference.join(' / ') : 'not configured',
)

function showToast(msg, ms = 2500) {
  toast.value = msg
  setTimeout(() => (toast.value = ''), ms)
}

onMounted(() => store.load())

async function runFull() {
  applying.value = true
  try {
    const { errno, stderr } = await applyFull()
    showToast(errno === 0 ? 'Applied ✓' : `Error: ${stderr}`)
    if (errno === 0) await store.load()
  } finally {
    applying.value = false
  }
}
</script>

<style scoped>
.fade-enter-active, .fade-leave-active { transition: opacity 0.2s; }
.fade-enter-from, .fade-leave-to { opacity: 0; }
</style>
