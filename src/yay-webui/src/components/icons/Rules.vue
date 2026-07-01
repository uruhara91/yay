<template>
  <svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor">
    <path v-if="active"
      d="M3 18v-2h6v2H3zm0-5v-2h12v2H3zm0-5V6h18v2H3z"/>
    <path v-else
      d="M3 18v-2h6v2H3zm0-5v-2h12v2H3zm0-5V6h18v2H3z" opacity=".7"/>
  </svg>
</template>
<script setup>
defineProps({ active: Boolean })
</script>
