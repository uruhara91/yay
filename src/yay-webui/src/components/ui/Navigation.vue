<template>
  <nav ref="navEl"
    class="fixed bottom-0 left-0 right-0 flex items-end
           bg-surface-container/95 backdrop-blur-md shadow-lg z-50"
    :style="{
      paddingBottom: 'var(--window-inset-bottom, env(safe-area-inset-bottom, 0px))',
    }">
    <div class="w-full h-16 flex items-center justify-around">
      <router-link v-for="item in items" :key="item.path" :to="item.path"
        class="flex-1 flex flex-col items-center justify-center gap-1 py-1
               text-xs font-medium transition-colors duration-200 no-underline"
        :class="active(item) ? 'text-on-surface' : 'text-on-surface-variant'">
        <div class="h-8 flex items-center justify-center rounded-full px-4 transition-all duration-200"
          :class="active(item) ? 'bg-secondary-container' : ''">
          <component :is="item.icon" :active="active(item)" />
        </div>
        <span>{{ item.label }}</span>
      </router-link>
    </div>
  </nav>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount } from 'vue'
import { useRoute } from 'vue-router'
import IconHome     from '@/components/icons/Home.vue'
import IconRules    from '@/components/icons/Rules.vue'
import IconGame     from '@/components/icons/Game.vue'
import IconLog      from '@/components/icons/Log.vue'

const route = useRoute()
const navEl = ref(null)
let ro = null

const items = [
  { path: '/',       label: 'Home',     icon: IconHome  },
  { path: '/rules',  label: 'Rules',    icon: IconRules },
  { path: '/games',  label: 'Games',    icon: IconGame  },
  { path: '/log',    label: 'Log',      icon: IconLog   },
]

const active = (item) =>
  item.path === '/' ? route.path === '/' : route.path.startsWith(item.path)

onMounted(() => {
  ro = new ResizeObserver(([e]) => {
    const h = e.borderBoxSize?.[0]?.blockSize ?? e.target.offsetHeight
    document.documentElement.style.setProperty('--nav-height', `${h}px`)
  })
  ro.observe(navEl.value)
})
onBeforeUnmount(() => ro?.disconnect())
</script>
