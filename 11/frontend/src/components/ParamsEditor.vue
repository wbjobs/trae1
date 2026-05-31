<template>
  <div>
    <div class="tabs-wrap">
      <el-radio-group v-model="activeTab" size="small">
        <el-radio-button label="query">Query 参数</el-radio-button>
        <el-radio-button label="path">Path 参数</el-radio-button>
        <el-radio-button label="header">Header 参数</el-radio-button>
      </el-radio-group>
      <el-button size="small" :icon="Plus" @click="addRow">添加</el-button>
    </div>

    <el-table :data="currentList" border size="small">
      <el-table-column label="启用" width="60">
        <template #default="{ row }">
          <el-checkbox v-model="row.enabled" />
        </template>
      </el-table-column>
      <el-table-column label="参数名" width="160">
        <template #default="{ row }">
          <el-input v-model="row.name" size="small" />
        </template>
      </el-table-column>
      <el-table-column label="类型" width="110">
        <template #default="{ row }">
          <el-select v-model="row.type" size="small" style="width: 100%">
            <el-option v-for="t in types" :key="t" :label="t" :value="t" />
          </el-select>
        </template>
      </el-table-column>
      <el-table-column label="必填" width="70">
        <template #default="{ row }">
          <el-switch v-model="row.required" />
        </template>
      </el-table-column>
      <el-table-column label="描述">
        <template #default="{ row }">
          <el-input v-model="row.description" size="small" />
        </template>
      </el-table-column>
      <el-table-column label="示例" width="140">
        <template #default="{ row }">
          <el-input v-model="row.example" size="small" />
        </template>
      </el-table-column>
      <el-table-column label="操作" width="70">
        <template #default="{ $index }">
          <el-button size="small" text type="danger" @click="removeRow($index)">删除</el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-divider>Body 参数 (JSON Schema)</el-divider>
    <div class="flex">
      <el-button size="small" :icon="Plus" @click="addBodyField">添加字段</el-button>
    </div>
    <el-table :data="bodyFields" border size="small" style="margin-top: 8px">
      <el-table-column label="字段名" width="160">
        <template #default="{ row }">
          <el-input v-model="row.name" size="small" />
        </template>
      </el-table-column>
      <el-table-column label="类型" width="110">
        <template #default="{ row }">
          <el-select v-model="row.type" size="small" style="width: 100%">
            <el-option v-for="t in types" :key="t" :label="t" :value="t" />
          </el-select>
        </template>
      </el-table-column>
      <el-table-column label="必填" width="70">
        <template #default="{ row }">
          <el-switch v-model="row.required" />
        </template>
      </el-table-column>
      <el-table-column label="描述">
        <template #default="{ row }">
          <el-input v-model="row.description" size="small" />
        </template>
      </el-table-column>
      <el-table-column label="示例" width="140">
        <template #default="{ row }">
          <el-input v-model="row.example" size="small" />
        </template>
      </el-table-column>
      <el-table-column label="操作" width="70">
        <template #default="{ $index }">
          <el-button size="small" text type="danger" @click="bodyFields.splice($index, 1)">删除</el-button>
        </template>
      </el-table-column>
    </el-table>
  </div>
</template>

<script setup lang="ts">
import { ref, watch } from 'vue'
import { Plus } from '@element-plus/icons-vue'

const props = defineProps<{
  params?: { query: any[]; path: any[]; header: any[] }
  body?: any
}>()

const types = ['string', 'number', 'integer', 'boolean', 'object', 'array']
const activeTab = ref('query')
const bodyFields = ref<any[]>([])

const getList = () => props.params || { query: [], path: [], header: [] }

const currentList = ref<any[]>([])

watch(
  () => props.params,
  (v) => {
    const data = v || { query: [], path: [], header: [] }
    currentList.value = (data[activeTab.value] || []).map((x) => ({ ...x }))
  },
  { immediate: true, deep: true }
)

watch(activeTab, () => {
  const data = getList()
  currentList.value = (data[activeTab.value] || []).map((x) => ({ ...x }))
})

watch(currentList, (v) => {
  if (!props.params) return
  props.params[activeTab.value] = v.map((x) => ({ ...x }))
}, { deep: true })

watch(
  () => props.body,
  (v) => {
    const p = v?.properties || {}
    bodyFields.value = Object.keys(p).map((k) => ({
      name: k,
      ...p[k],
      required: v?.required?.includes(k) || false
    }))
  },
  { immediate: true, deep: true }
)

function addRow() {
  currentList.value.push({ name: '', type: 'string', required: false, description: '', example: '', enabled: true })
}
function removeRow(idx: number) {
  currentList.value.splice(idx, 1)
}
function addBodyField() {
  bodyFields.value.push({ name: '', type: 'string', required: false, description: '', example: '' })
}
function syncBody() {
  if (!props.body) return
  const properties: any = {}
  const required: string[] = []
  bodyFields.value.forEach((f) => {
    if (!f.name) return
    properties[f.name] = {
      type: f.type,
      description: f.description,
      example: f.example
    }
    if (f.required) required.push(f.name)
  })
  props.body.type = 'object'
  props.body.properties = properties
  props.body.required = required
}
watch(bodyFields, syncBody, { deep: true })
</script>

<style scoped>
.tabs-wrap {
  display: flex;
  gap: 12px;
  align-items: center;
  margin-bottom: 12px;
}
</style>
