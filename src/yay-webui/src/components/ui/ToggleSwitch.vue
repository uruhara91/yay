<template>
  <label class="relative inline-flex cursor-pointer items-center"
    :class="{ 'opacity-40 pointer-events-none': disabled }">
    <input type="checkbox" class="peer sr-only"
      :checked="modelValue" :disabled="disabled"
      @change="$emit('update:modelValue', $event.target.checked)" />
    <div class="relative h-8 w-13 rounded-full border-2 transition-colors duration-150"
      :class="modelValue
        ? 'border-primary bg-primary'
        : 'border-outline bg-surface-container-highest'">
      <div class="absolute top-1/2 -translate-y-1/2 rounded-full shadow-sm transition-all duration-150 flex items-center justify-center"
        :class="modelValue
          ? 'h-6 w-6 bg-on-primary translate-x-6'
          : 'h-4 w-4 bg-outline translate-x-1.5'">
        <svg class="h-4 w-4 transition-all duration-200"
          :class="modelValue ? 'opacity-100 scale-100' : 'opacity-0 scale-50'"
          viewBox="0 0 24 24" fill="currentColor">
          <path d="M9.55 18L3.85 12.3L5.275 10.875L9.55 15.15L18.725 5.975L20.15 7.4L9.55 18Z"/>
        </svg>
      </div>
    </div>
  </label>
</template>
<script setup>
defineProps({ modelValue: Boolean, disabled: { type: Boolean, default: false } })
defineEmits(['update:modelValue'])
</script>
