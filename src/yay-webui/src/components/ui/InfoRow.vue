<template>
  <div class="flex items-start gap-3 px-4 py-3">
    <span class="text-base shrink-0 mt-0.5">{{ icon }}</span>
    <div class="min-w-0 flex-1">
      <p class="text-xs text-on-surface-variant">{{ label }}</p>
      <p class="text-sm text-on-surface mt-0.5" :class="mono ? 'font-mono text-xs' : ''">{{ value }}</p>
    </div>
  </div>
</template>
<script setup>
defineProps({ icon: String, label: String, value: String, mono: Boolean })
</script>
