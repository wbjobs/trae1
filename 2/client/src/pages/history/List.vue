<template>
  <div class="history-page">
    <div class="page-header">
      <el-button text :icon="ArrowLeft" @click="goBack">
        返回
      </el-button>
      <h2>历史版本</h2>
      <span class="doc-title">{{ document?.title }}</span>
      <el-tag type="info">当前版本 v{{ document?.version }}</el-tag>
    </div>

    <div class="page-content">
      <HistoryVersion
        :versions="versions"
        :current-version="document?.version"
        @restore="handleRestore"
        @refresh="fetchVersions"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useDocumentStore } from '@/stores/document'
import { ElMessage } from 'element-plus'
import { ArrowLeft } from '@element-plus/icons-vue'
import HistoryVersion from '@components/HistoryVersion/Index.vue'

const router = useRouter()
const route = useRoute()
const documentStore = useDocumentStore()

const documentId = computed(() => route.params.id)
const document = computed(() => documentStore.currentDocument)
const versions = computed(() => documentStore.versions)

const goBack = () => {
  router.back()
}

const loadData = async () => {
  try {
    await documentStore.fetchDocument(documentId.value)
    await documentStore.fetchVersions(documentId.value)
  } catch (error) {
    console.error('Load history error:', error)
  }
}

const handleRestore = (documentData) => {
  if (documentData) {
    ElMessage.success('版本恢复成功')
    documentStore.currentDocument = documentData
  }
}

onMounted(() => {
  loadData()
})
</script>

<style lang="scss" scoped>
.history-page {
  padding: 24px;
  max-width: 900px;
  margin: 0 auto;
}

.page-header {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-bottom: 24px;

  h2 {
    margin: 0;
    font-size: 20px;
    font-weight: 600;
    color: #303133;
  }

  .doc-title {
    font-size: 14px;
    color: #909399;
  }
}

.page-content {
  background: #fff;
  border-radius: 8px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);
}

@media (max-width: 768px) {
  .history-page {
    padding: 16px;
  }

  .page-header {
    flex-wrap: wrap;

    h2 {
      font-size: 18px;
    }
  }
}
</style>
