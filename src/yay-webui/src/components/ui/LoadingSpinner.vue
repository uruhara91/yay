<template>
  <div class="flex justify-center py-8">
    <svg class="animate-spin text-primary" :width="size" :height="size" viewBox="0 0 24 24" fill="none">
      <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="3"/>
      <path class="opacity-75" fill="currentColor"
        d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z"/>
    </svg>
  </div>
</template>
<script setup>
defineProps({ size: { default: 36 } })
</script>
