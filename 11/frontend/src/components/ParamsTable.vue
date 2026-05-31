<template>
  <div>
    <template v-if="params">
      <template v-if="params.query?.length">
        <h4>Query 参数</h4>
        <el-table :data="params.query" border size="small">
          <el-table-column prop="name" label="参数名" width="160" />
          <el-table-column prop="type" label="类型" width="100" />
          <el-table-column prop="required" label="必填" width="80">
            <template #default="{ row }">{{ row.required ? '是' : '否' }}</template>
          </el-table-column>
          <el-table-column prop="description" label="描述" />
          <el-table-column prop="example" label="示例" width="140" />
        </el-table>
      </template>
      <template v-if="params.path?.length">
        <h4 style="margin-top: 16px">Path 参数</h4>
        <el-table :data="params.path" border size="small">
          <el-table-column prop="name" label="参数名" width="160" />
          <el-table-column prop="type" label="类型" width="100" />
          <el-table-column prop="required" label="必填" width="80">
            <template #default="{ row }">{{ row.required ? '是' : '否' }}</template>
          </el-table-column>
          <el-table-column prop="description" label="描述" />
          <el-table-column prop="example" label="示例" width="140" />
        </el-table>
      </template>
      <template v-if="params.header?.length">
        <h4 style="margin-top: 16px">Header 参数</h4>
        <el-table :data="params.header" border size="small">
          <el-table-column prop="name" label="参数名" width="160" />
          <el-table-column prop="type" label="类型" width="100" />
          <el-table-column prop="required" label="必填" width="80">
            <template #default="{ row }">{{ row.required ? '是' : '否' }}</template>
          </el-table-column>
          <el-table-column prop="description" label="描述" />
        </el-table>
      </template>
    </template>
    <h4 style="margin-top: 16px" v-if="body && Object.keys(body).length">Body 参数</h4>
    <JsonViewer :data="body" v-if="body && Object.keys(body).length" />
  </div>
</template>

<script setup lang="ts">
defineProps<{
  params?: { query?: any[]; path?: any[]; header?: any[] }
  body?: any
}>()
</script>
