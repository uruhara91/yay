<template>
  <div
    ref="el"
    class="ripple-wrapper"
    :class="{ 'non-touch': !touch }"
    :style="hoverStyle"
    :tabindex="tabindex"
    @pointerdown="onDown"
    @keydown="onKey"
  >
    <slot />
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
const props = defineProps({
  tabindex: { default: '0' },
  color:    { default: null },
})
const el    = ref(null)
const touch = ref(false)
const hover = ref('')

const hoverStyle = computed(() =>
  !touch.value && hover.value ? { '--hover-color': hover.value } : {}
)

onMounted(() => {
  touch.value = 'ontouchstart' in window || navigator.maxTouchPoints > 0
  const c = getComputedStyle(el.value).color
  hover.value = c.includes('rgba(0,0,0,0)')
    ? 'rgba(255,255,255,0.09)'
    : c.replace('rgb', 'rgba').replace(')', ', 0.09)')
})

function onDown(e) {
  const node = el.value
  const r    = node.getBoundingClientRect()
  const size = Math.max(r.width, r.height)
  const dur  = Math.min(0.8, Math.max(0.2, 0.2 + (r.width / 800) * 0.3))
  const span = document.createElement('span')
  span.className = 'ripple'
  const col = getComputedStyle(node).color
  span.style.cssText = `
    width:${size}px;height:${size}px;
    left:${e.clientX - r.left - size / 2}px;
    top:${e.clientY - r.top - size / 2}px;
    animation-duration:${dur}s;
    transition:opacity ${dur}s ease;
    background-color:${col.replace('rgb', 'rgba').replace(')', ', 0.2)')};
  `
  node.appendChild(span)
  const end = () => {
    span.classList.add('end')
    setTimeout(() => span.remove(), dur * 1000)
    document.removeEventListener('pointerup', end)
    document.removeEventListener('pointercancel', end)
  }
  document.addEventListener('pointerup', end)
  document.addEventListener('pointercancel', end)
}

function onKey(e) {
  if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); el.value.click() }
}
</script>
