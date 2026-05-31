<template>
  <div class="page-container">
    <div class="page-header">
      <div class="flex" style="gap: 12px; align-items: center">
        <el-button :icon="ArrowLeft" @click="$router.back()">返回</el-button>
        <span class="page-title">文档详情</span>
      </div>
      <div>
        <el-button :icon="Edit" @click="gotoEdit">编辑</el-button>
        <el-button type="primary" :icon="Connection" @click="gotoDebug">在线调试</el-button>
      </div>
    </div>

    <el-card>
      <h2>
        <span class="tag-method" :class="methodColor(doc.method)">{{ doc.method }}</span>
        {{ doc.title }}
      </h2>
      <div style="color: #909399; font-family: monospace; margin: 8px 0">{{ doc.path }}</div>
      <el-descriptions :column="2" border style="margin-top: 16px">
        <el-descriptions-item label="所属项目">{{ doc.projectId }}</el-descriptions-item>
        <el-descriptions-item label="分类">{{ doc.category || '-' }}</el-descriptions-item>
        <el-descriptions-item label="创建时间">{{ formatDate(doc.createdAt) }}</el-descriptions-item>
        <el-descriptions-item label="更新时间">{{ formatDate(doc.updatedAt) }}</el-descriptions-item>
        <el-descriptions-item label="标签">
          <el-tag v-for="t in doc.tags || []" :key="t" style="margin-right: 6px">{{ t }}</el-tag>
        </el-descriptions-item>
      </el-descriptions>

      <el-tabs v-model="activeTab" style="margin-top: 16px">
        <el-tab-pane label="描述" name="desc">
          <div class="md-content" v-html="renderedContent"></div>
        </el-tab-pane>
        <el-tab-pane label="请求参数" name="params">
          <ParamsTable :params="doc.requestParams" :body="doc.requestBody" />
        </el-tab-pane>
        <el-tab-pane label="响应示例" name="response">
          <JsonViewer :data="doc.response" />
        </el-tab-pane>
        <el-tab-pane label="版本历史" name="versions">
          <VersionList :doc-id="doc.id" />
        </el-tab-pane>
      </el-tabs>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ArrowLeft, Edit, Connection } from '@element-plus/icons-vue'
import { marked } from 'marked'
import { docApi } from '@/api/doc'
import { formatDate, methodColor } from '@/utils'
import ParamsTable from '@/components/ParamsTable.vue'
import JsonViewer from '@/components/JsonViewer.vue'
import VersionList from '@/components/VersionList.vue'

const route = useRoute()
const router = useRouter()
const doc = ref<any>({})
const activeTab = ref('desc')

const renderedContent = computed(() => {
  try {
    return marked(doc.value.content || doc.value.description || '暂无描述')
  } catch (e) {
    return doc.value.content || doc.value.description || ''
  }
})

onMounted(async () => {
  try {
    const res = await docApi.getDetail(route.params.docId)
    doc.value = res.data
  } catch (e) {}
})

function gotoEdit() {
  router.push(`/docs/${route.params.docId}/edit`)
}
function gotoDebug() {
  router.push(`/docs/${route.params.docId}/debug`)
}
</script>

<style scoped>
.md-content :deep(pre) {
  background: #f5f7fa;
  padding: 12px;
  border-radius: 4px;
  overflow-x: auto;
}
</style>
