<template>
  <div class="relative flex items-center justify-center p-2 rounded-full cursor-pointer">
    <input
      type="checkbox"
      class="peer sr-only"
      :checked="modelValue"
      :disabled="disabled"
      @change="$emit('update:modelValue', $event.target.checked)"
    />
    <div
      class="relative w-4.5 h-4.5 rounded-xs border-2 transition-colors duration-200 flex items-center justify-center"
      :class="[
        modelValue ? 'border-primary bg-primary' : 'border-on-surface-variant bg-transparent',
        disabled ? 'opacity-38' : '',
      ]"
    >
      <svg
        class="w-3.5 h-3.5 text-on-primary transition-all pointer-events-none"
        :class="modelValue ? 'scale-100 opacity-100' : 'scale-50 opacity-0'"
        viewBox="0 0 24 24"
        fill="none"
        stroke="currentColor"
        stroke-width="3"
        stroke-linecap="round"
        stroke-linejoin="round"
      >
        <polyline points="20 6 9 17 4 12"></polyline>
      </svg>
    </div>
  </div>
</template>

<script setup>
defineProps({
  modelValue: { type: Boolean, default: false },
  disabled: { type: Boolean, default: false },
})
defineEmits(['update:modelValue'])
</script>
