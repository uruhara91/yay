<template>
  <div class="page h-full flex flex-col">
    <div class="sticky top-0 z-10 bg-background">
      <div class="max-w-2xl mx-auto px-5 pt-5 pb-3">
        <h1 class="text-xl font-semibold text-on-surface">yay</h1>
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
          <div>
            <p class="text-sm font-semibold text-on-secondary-container">yay v{{ version }}</p>
            <p class="text-xs text-on-secondary-container/70 mt-0.5">System tweaks · No daemon</p>
          </div>
        </div>

        <!-- Info card -->
        <div class="bg-surface-container rounded-2xl divide-y divide-outline-variant/20">
          <InfoRow icon="⚙️" label="I/O Scheduler" :value="ioInfo" />
          <InfoRow icon="📦" label="Rules" :value="`${componentCount} components · ${appopCount} appops`" />
          <InfoRow icon="🎮" label="Game mode" :value="`${gameCount} games configured`" />
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
                <svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor" class="text-on-primary-container">
                  <path d="M17.65 6.35A7.958 7.958 0 0012 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99 0 0112 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z"/>
                </svg>
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
                <svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor" class="text-on-primary-container">
                  <path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8l-6-6zm2 14H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z"/>
                </svg>
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
import { ref, onMounted } from 'vue'
import { readConfig, applyFull } from '@/helpers/bridge'
import Ripple        from '@/components/ui/Ripple.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'
import Snackbar      from '@/components/ui/Snackbar.vue'
import InfoRow       from '@/components/ui/InfoRow.vue'

const version        = ref('1.1')
const componentCount = ref(0)
const appopCount     = ref(0)
const gameCount      = ref(0)
const ioInfo         = ref('kyber / mq-deadline / deadline')
const applying       = ref(false)
const toast          = ref('')

function showToast(msg, ms = 2500) {
  toast.value = msg
  setTimeout(() => (toast.value = ''), ms)
}

onMounted(async () => {
  try {
    const [rules, games] = await Promise.all([
      readConfig('rules.json').catch(() => ({ components: [], appops: [] })),
      readConfig('game_config.json').catch(() => ({ games: [] })),
    ])
    componentCount.value = (rules.components ?? []).filter(c => c.enabled !== false).length
    appopCount.value     = (rules.appops     ?? []).filter(a => a.enabled !== false).length
    gameCount.value      = (games.games      ?? []).filter(g => g.enabled !== false).length
  } catch {}
})

async function runFull() {
  applying.value = true
  try {
    const { errno, stderr } = await applyFull()
    showToast(errno === 0 ? 'Applied ✓' : `Error: ${stderr}`)
  } finally {
    applying.value = false
  }
}
</script>

<style scoped>
.fade-enter-active, .fade-leave-active { transition: opacity 0.2s; }
.fade-enter-from, .fade-leave-to { opacity: 0; }
</style>
