<template>
  <div>
    <template v-if="isText">
      <el-input
        :model-value="textValue"
        type="textarea"
        :rows="16"
        placeholder="请输入 JSON 文本，例如 {key: value}"
        @input="onTextInput"
      />
    </template>
    <template v-else>
      <div class="kv-toolbar">
        <el-button size="small" :icon="Plus" @click="addRow">添加</el-button>
        <el-button size="small" :icon="Delete" @click="removeSelected" :disabled="!selected.length">删除</el-button>
      </div>
      <el-table :data="model" border size="small" @selection-change="s => (selected = s)">
        <el-table-column type="selection" width="40" />
        <el-table-column label="启用" width="60">
          <template #default="{ row }">
            <el-checkbox v-model="row.enabled" />
          </template>
        </el-table-column>
        <el-table-column label="Key" prop="key" min-width="160">
          <template #default="{ row }">
            <el-input v-model="row.key" size="small" />
          </template>
        </el-table-column>
        <el-table-column v-if="!allowFiles" label="Value" prop="value" min-width="200">
          <template #default="{ row }">
            <el-input v-model="row.value" size="small" />
          </template>
        </el-table-column>
        <el-table-column v-else label="Value" prop="value" min-width="200">
          <template #default="{ row }">
            <el-input v-if="!row.file" v-model="row.value" size="small" />
            <el-upload
              v-else
              :auto-upload="false"
              :show-file-list="false"
              @change="(f) => (row.file = f)"
            >
              <el-button size="small">{{ row.file?.name || '选择文件' }}</el-button>
            </el-upload>
            <el-button size="small" text @click="row.file = null" v-if="row.file">清除</el-button>
            <el-switch
              v-model="row.isFile"
              size="small"
              active-text="文件"
              inactive-text="文本"
              style="margin-left: 8px"
            />
          </template>
        </el-table-column>
        <el-table-column label="描述" min-width="180">
          <template #default="{ row }">
            <el-input v-model="row.description" size="small" placeholder="描述" />
          </template>
        </el-table-column>
        <el-table-column label="操作" width="70">
          <template #default="{ $index }">
            <el-button size="small" text type="danger" @click="model.splice($index, 1)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </template>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { Plus, Delete } from '@element-plus/icons-vue'

const props = defineProps<{
  modelValue: any
  allowFiles?: boolean
  isText?: boolean
}>()
const emit = defineEmits<{
  (e: 'update:modelValue', v: any): void
}>()

const selected = ref<any[]>([])

const model = computed<any[]>({
  get: () => {
    if (props.isText) return []
    if (!Array.isArray(props.modelValue)) return []
    return props.modelValue
  },
  set: (v) => emit('update:modelValue', v)
})

const textValue = computed({
  get: () => {
    if (props.isText) {
      return typeof props.modelValue === 'string' ? props.modelValue : ''
    }
    return ''
  },
  set: () => {}
})

function onTextInput(v: string | Event) {
  const text = typeof v === 'string' ? v : (v.target as HTMLTextAreaElement)?.value ?? ''
  emit('update:modelValue', text)
}

function addRow() {
  model.value = [...model.value, { key: '', value: '', enabled: true, description: '' }]
}
function removeSelected() {
  const keys = new Set(selected.value.map((r) => r.key))
  model.value = model.value.filter((r) => !keys.has(r.key))
}

watch(
  () => props.modelValue,
  (v) => {
    if (!props.isText && !Array.isArray(v) && !v) {
      emit('update:modelValue', [])
    }
  },
  { immediate: true }
)
</script>

<style scoped>
.kv-toolbar {
  margin-bottom: 8px;
}
</style>
