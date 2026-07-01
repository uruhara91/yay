<template>
  <svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor">
    <path v-if="active"
      d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8l-6-6zm4 18H6V4h7v5h5v11z"/>
    <path v-else
      d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8l-6-6zm4 18H6V4h7v5h5v11zM8 15h8v2H8zm0-4h8v2H8zm0-4h5v2H8z"/>
  </svg>
</template>
<script setup>
defineProps({ active: Boolean })
</script>
