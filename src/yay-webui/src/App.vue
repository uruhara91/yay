<template>
  <div id="app" class="h-screen flex flex-col bg-background text-on-surface overflow-hidden">
    <main class="main-content flex-1 min-h-0 md:ml-20 overflow-hidden relative">
      <router-view v-slot="{ Component, route }">
        <transition :name="transitionName">
          <keep-alive>
            <component :is="Component" :key="route.name" />
          </keep-alive>
        </transition>
      </router-view>
    </main>
    <Navigation />
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import { useRoute } from 'vue-router'
import { topLevelRoutes } from '@/router'
import Navigation from '@/components/ui/Navigation.vue'

const route = useRoute()
const transitionName = ref('')

watch(
  () => route.path,
  (to, from) => {
    // Sibling top-level tabs (Home/Rules/Games/IO/Log/Settings): no slide.
    if (topLevelRoutes.includes(to) && topLevelRoutes.includes(from)) {
      transitionName.value = ''
      return
    }

    const isOpeningChild = to.startsWith(from === '/' ? '' : from) && to.length > from.length
    const isClosingChild = from.startsWith(to === '/' ? '' : to) && from.length > to.length

    if (isOpeningChild) transitionName.value = 'page-open'
    else if (isClosingChild) transitionName.value = 'page-close'
    else transitionName.value = ''
  },
)
</script>

<style>
.page-open-enter-active,
.page-open-leave-active,
.page-close-enter-active,
.page-close-leave-active {
  transition: all 150ms cubic-bezier(0.2, 0, 0, 1);
  position: absolute;
  width: 100%;
  top: 0;
  bottom: 0;
  left: 0;
  will-change: transform, opacity;
  background-color: var(--color-background);
}

.page-open-enter-active { z-index: 2; }
.page-open-leave-active { z-index: 1; }
.page-open-leave-to { transform: translateX(-15%); }
.page-open-enter-from { transform: translateX(15%); opacity: 0; }
.page-open-enter-to { transform: translateX(0); opacity: 1; }

.page-close-leave-active { z-index: 2; }
.page-close-enter-active { z-index: 1; }
.page-close-leave-from { transform: scale(1); opacity: 1; }
.page-close-leave-to { transform: scale(0.95); opacity: 0; }
.page-close-enter-from { transform: translateX(-15%); }
.page-close-enter-to { transform: translateX(0); }
</style>
