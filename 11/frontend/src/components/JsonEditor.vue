<template>
  <div>
    <div class="flex-between" style="margin-bottom: 8px">
      <span style="font-weight: 500">{{ title }}</span>
      <el-button size="small" @click="formatJson">格式化</el-button>
    </div>
    <div ref="editorRef" class="json-editor"></div>
    <div v-if="error" class="error">{{ error }}</div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch, nextTick, computed } from 'vue'
import * as monaco from 'monaco-editor'

const props = defineProps<{ modelValue: any; title?: string }>()
const emit = defineEmits<(e: 'update:modelValue', v: any) => void>()

const editorRef = ref<HTMLElement>()
const error = ref('')
let editor: monaco.editor.IStandaloneCodeEditor | null = null

const modelValue = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v)
})

function toText(v: any) {
  if (v == null) return ''
  if (typeof v === 'string') return v
  try {
    return JSON.stringify(v, null, 2)
  } catch (e) {
    return String(v)
  }
}

function parseText(text: string) {
  const t = text.trim()
  if (!t) return {}
  try {
    return JSON.parse(t)
  } catch (e) {
    return text
  }
}

function initEditor() {
  if (!editorRef.value) return
  editor = monaco.editor.create(editorRef.value, {
    value: toText(modelValue.value),
    language: 'json',
    theme: 'vs',
    automaticLayout: true,
    minimap: { enabled: false },
    fontSize: 13
  })
  editor.onDidChangeModelContent(() => {
    const v = editor?.getValue() || ''
    const t = v.trim()
    if (!t) {
      modelValue.value = {}
      error.value = ''
      return
    }
    try {
      const parsed = JSON.parse(t)
      modelValue.value = parsed
      error.value = ''
    } catch (e) {
      modelValue.value = v
      error.value = ''
    }
  })
}

function formatJson() {
  const v = editor?.getValue() || ''
  try {
    editor?.setValue(JSON.stringify(JSON.parse(v), null, 2))
  } catch (e) {
    error.value = 'JSON 格式错误'
  }
}

onMounted(initEditor)
watch(
  () => props.modelValue,
  (v) => {
    nextTick(() => {
      if (editor) {
        const cur = editor.getValue()
        const next = toText(v)
        if (cur !== next) editor.setValue(next)
      }
    })
  }
)
</script>

<style scoped>
.json-editor {
  height: 360px;
  border: 1px solid #ebeef5;
  border-radius: 4px;
}
.error {
  color: #f56c6c;
  font-size: 12px;
  margin-top: 4px;
}
</style>
