<template>
  <pre class="json-viewer"><code>{{ formatted }}</code></pre>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{ data: any }>()

const formatted = computed(() => {
  if (props.data == null) return ''
  if (typeof props.data === 'string') {
    try {
      return JSON.stringify(JSON.parse(props.data), null, 2)
    } catch (e) {
      return props.data
    }
  }
  try {
    return JSON.stringify(props.data, null, 2)
  } catch (e) {
    return String(props.data)
  }
})
</script>

<style scoped>
.json-viewer {
  background: #f6f8fa;
  padding: 16px;
  border-radius: 4px;
  overflow-x: auto;
  font-family: Menlo, Monaco, Consolas, monospace;
  font-size: 13px;
  line-height: 1.6;
  max-height: 600px;
}
</style>
