<template>
  <Transition name="modal">
    <div v-if="show"
      class="fixed inset-0 z-50 flex items-end justify-center bg-black/60 sm:items-center"
      @click="closeOnOutsideClick && $emit('close')">
      <div class="bg-surface rounded-t-3xl sm:rounded-2xl w-full max-w-md max-h-[85vh]
                  flex flex-col overflow-hidden shadow-xl mx-0 sm:mx-4 modal-container"
        @click.stop>
        <div class="p-6 pb-3">
          <h2 class="text-xl font-medium text-on-surface">{{ title }}</h2>
          <p v-if="description" class="mt-1 text-sm text-on-surface-variant">{{ description }}</p>
        </div>
        <div class="overflow-y-auto flex-1 px-2"><slot /></div>
        <div class="p-4 pt-2 flex justify-end gap-2"><slot name="actions" /></div>
      </div>
    </div>
  </Transition>
</template>
<script setup>
defineProps({
  show: Boolean, title: String, description: String,
  closeOnOutsideClick: { type: Boolean, default: true },
})
defineEmits(['close'])
</script>
<style scoped>
.modal-enter-active, .modal-leave-active {
  transition: opacity 200ms cubic-bezier(0.2,0,0,1);
}
.modal-enter-active .modal-container, .modal-leave-active .modal-container {
  transition: transform 200ms cubic-bezier(0.2,0,0,1);
}
.modal-enter-from, .modal-leave-to { opacity: 0; }
.modal-enter-from .modal-container, .modal-leave-to .modal-container {
  transform: translateY(100%) scaleY(0.95);
}
@media (min-width: 640px) {
  .modal-enter-from .modal-container, .modal-leave-to .modal-container {
    transform: scale(0.95) translateY(0);
  }
}
</style>
