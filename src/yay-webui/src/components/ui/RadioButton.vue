<template>
  <div class="inline-flex items-center group">
    <input
      :id="id"
      type="radio"
      :name="name"
      :value="value"
      :checked="modelValue === value"
      :disabled="disabled"
      @change="handleChange"
      class="absolute opacity-0 w-0 h-0 peer"
    />
    <label
      :for="id"
      class="relative inline-flex items-center"
      :class="disabled ? 'cursor-not-allowed' : 'cursor-pointer'"
    >
      <span class="relative inline-block h-6 w-6">
        <span
          class="absolute rounded-full border transition-all duration-150 ease-in-out top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 h-5 w-5"
          :class="modelValue === value
            ? 'border-primary border-2'
            : disabled ? 'border-outline border' : 'border-outline-variant border'"
        ></span>
        <span
          class="absolute rounded-full bg-primary transition-transform duration-150 ease-in-out top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 h-2.5 w-2.5"
          :class="modelValue === value ? 'scale-100' : 'scale-0'"
        ></span>
      </span>
      <span v-if="label" class="text-on-surface select-none ms-3 text-sm" :class="disabled ? 'opacity-60' : ''">
        {{ label }}
      </span>
    </label>
  </div>
</template>

<script setup>
const props = defineProps({
  id: { type: String, default: () => `radio-${Math.random().toString(36).slice(2, 9)}` },
  name: String,
  value: [String, Number, Boolean],
  modelValue: [String, Number, Boolean],
  disabled: { type: Boolean, default: false },
  label: { type: String, default: '' },
})

const emit = defineEmits(['update:modelValue'])

function handleChange(event) {
  if (!props.disabled) emit('update:modelValue', event.target.value)
}
</script>
