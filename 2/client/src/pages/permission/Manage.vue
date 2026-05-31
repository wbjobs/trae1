<template>
  <div class="permission-page">
    <div class="page-header">
      <el-button text :icon="ArrowLeft" @click="goBack">
        返回
      </el-button>
      <h2>权限管理</h2>
      <span class="doc-title">{{ document?.title }}</span>
    </div>

    <div class="page-content">
      <PermissionControl
        :document="document"
        :permissions="permissions"
        @update="handlePermissionUpdate"
        @add="handlePermissionAdd"
        @remove="handlePermissionRemove"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useDocumentStore } from '@/stores/document'
import { getDocumentPermissions } from '@/utils/api'
import { ElMessage } from 'element-plus'
import { ArrowLeft } from '@element-plus/icons-vue'
import PermissionControl from '@components/PermissionControl/Index.vue'

const router = useRouter()
const route = useRoute()
const documentStore = useDocumentStore()

const documentId = computed(() => route.params.id)
const document = computed(() => documentStore.currentDocument)
const permissions = ref(null)

const goBack = () => {
  router.back()
}

const loadData = async () => {
  try {
    await documentStore.fetchDocument(documentId.value)
    const response = await getDocumentPermissions(documentId.value)
    permissions.value = response.data
  } catch (error) {
    console.error('Load permissions error:', error)
  }
}

const handlePermissionUpdate = () => {
  ElMessage.success('权限更新成功')
  loadData()
}

const handlePermissionAdd = () => {
  ElMessage.success('添加成功')
  loadData()
}

const handlePermissionRemove = () => {
  ElMessage.success('移除成功')
  loadData()
}

onMounted(() => {
  loadData()
})
</script>

<style lang="scss" scoped>
.permission-page {
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
  .permission-page {
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
