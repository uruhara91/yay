<template>
  <svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor">
    <path v-if="active"
      d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 4a1.5 1.5 0 110 3 1.5 1.5 0 010-3zm4.5 12h-9v-1.2c0-2 3-3.05 4.5-3.05s4.5 1.05 4.5 3.05V18z"/>
    <path v-else
      d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 2c4.42 0 8 3.58 8 8s-3.58 8-8 8-8-3.58-8-8 3.58-8 8-8zm0 2a2 2 0 100 4 2 2 0 000-4zm0 5c-2.21 0-6 1.12-6 3.5V17h12v-2.5c0-2.38-3.79-3.5-6-3.5z"
      opacity=".85"/>
  </svg>
</template>
<script setup>
defineProps({ active: Boolean })
</script>
