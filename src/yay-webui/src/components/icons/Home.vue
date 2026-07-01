<template>
  <svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor">
    <path v-if="active" d="M10 20v-6h4v6h5v-8h3L12 3 2 12h3v8z"/>
    <path v-else d="M12 3L2 12h3v8h6v-5h2v5h6v-8h3L12 3zm0 2.7l5 4.5V18h-3v-5H10v5H7v-7.8l5-4.5z"/>
  </svg>
</template>
<script setup>
defineProps({ active: Boolean })
</script>
