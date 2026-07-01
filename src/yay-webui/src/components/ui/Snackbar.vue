<template>
  <Transition name="snack">
    <div v-if="show"
      class="fixed bottom-20 left-1/2 -translate-x-1/2 z-[100]
             bg-inverse-surface text-inverse-on-surface
             px-4 py-3 rounded-xl shadow-lg text-sm font-medium
             max-w-xs w-max text-center pointer-events-none">
      {{ message }}
    </div>
  </Transition>
</template>
<script setup>
defineProps({ show: Boolean, message: String })
</script>
<style scoped>
.snack-enter-active, .snack-leave-active { transition: all 0.2s ease; }
.snack-enter-from, .snack-leave-to { opacity: 0; transform: translate(-50%, 8px); }
</style>
