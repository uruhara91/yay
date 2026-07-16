<template>
  <div>
    <label class="block text-xs text-on-surface-variant mb-1">{{ label }}</label>
    <input
      :value="modelValue"
      :placeholder="placeholder"
      @input="$emit('update:modelValue', $event.target.value)"
      class="w-full bg-surface-container-high rounded-xl px-3 py-2.5
             text-sm text-on-surface placeholder-on-surface-variant/50
             border border-transparent focus:border-primary
             outline-none transition-colors" />
  </div>
</template>
<script setup>
defineProps({ modelValue: String, label: String, placeholder: { default: '' } })
defineEmits(['update:modelValue'])
</script>
