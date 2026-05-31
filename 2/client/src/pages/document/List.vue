<template>
  <div class="document-list-page">
    <div class="page-header">
      <div class="header-left">
        <h2>我的文档</h2>
        <el-input
          v-model="searchQuery"
          placeholder="搜索文档..."
          :prefix-icon="Search"
          clearable
          size="default"
          class="search-input"
          @input="handleSearch"
        />
      </div>
      <div class="header-right">
        <el-select v-model="filterStatus" placeholder="状态筛选" size="default" @change="fetchDocuments">
          <el-option label="全部" value="" />
          <el-option label="草稿" value="draft" />
          <el-option label="已发布" value="published" />
          <el-option label="已归档" value="archived" />
        </el-select>
        <el-button type="primary" :icon="Plus" @click="goToCreate">
          新建文档
        </el-button>
      </div>
    </div>

    <div class="document-grid">
      <div
        v-for="document in filteredDocuments"
        :key="document._id"
        class="document-card"
        @click="goToDetail(document._id)"
      >
        <div class="card-header">
          <el-icon class="doc-icon"><Document /></el-icon>
          <div class="card-actions" @click.stop>
            <el-dropdown trigger="click" @command="(cmd) => handleCommand(cmd, document)">
              <el-button text size="small">
                <el-icon><MoreFilled /></el-icon>
              </el-button>
              <template #dropdown>
                <el-dropdown-menu>
                  <el-dropdown-item command="edit">
                    <el-icon><Edit /></el-icon>
                    编辑
                  </el-dropdown-item>
                  <el-dropdown-item command="permissions">
                    <el-icon><Lock /></el-icon>
                    权限管理
                  </el-dropdown-item>
                  <el-dropdown-item command="versions">
                    <el-icon><Clock /></el-icon>
                    历史版本
                  </el-dropdown-item>
                  <el-dropdown-item divided command="delete" style="color: #F56C6C">
                    <el-icon><Delete /></el-icon>
                    删除
                  </el-dropdown-item>
                </el-dropdown-menu>
              </template>
            </el-dropdown>
          </div>
        </div>
        <div class="card-body">
          <h3 class="doc-title">{{ document.title }}</h3>
          <p class="doc-preview">{{ getPreview(document.content) }}</p>
        </div>
        <div class="card-footer">
          <div class="doc-tags">
            <el-tag
              v-for="tag in document.tags?.slice(0, 2)"
              :key="tag"
              size="small"
              type="info"
            >
              {{ tag }}
            </el-tag>
            <span v-if="document.tags?.length > 2" class="more-tags">
              +{{ document.tags.length - 2 }}
            </span>
          </div>
          <div class="doc-meta">
            <el-tag v-if="document.status === 'draft'" type="info" size="small">草稿</el-tag>
            <el-tag v-else-if="document.status === 'published'" type="success" size="small">已发布</el-tag>
            <el-tag v-else type="warning" size="small">已归档</el-tag>
            <span class="update-time">{{ formatTime(document.updatedAt) }}</span>
          </div>
        </div>
        <div class="card-hover-bg" />
      </div>

      <div v-if="!isLoading && filteredDocuments.length === 0" class="empty-state">
        <el-empty description="暂无文档">
          <el-button type="primary" :icon="Plus" @click="goToCreate">
            新建第一个文档
          </el-button>
        </el-empty>
      </div>
    </div>

    <div v-if="pagination.totalPages > 1" class="pagination">
      <el-pagination
        v-model:current-page="pagination.page"
        :page-size="pagination.limit"
        :total="pagination.total"
        layout="prev, pager, next"
        @current-change="fetchDocuments"
      />
    </div>

    <div v-if="isLoading" class="loading-overlay">
      <el-icon class="loading-icon" :size="40"><Loading /></el-icon>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useDocumentStore } from '@/stores/document'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Search, Plus, Document, Edit, Lock, Clock, Delete, MoreFilled, Loading } from '@element-plus/icons-vue'
import dayjs from '@/utils/dayjs'

const router = useRouter()
const documentStore = useDocumentStore()

const searchQuery = ref('')
const filterStatus = ref('')
const isLoading = ref(false)

const documents = computed(() => documentStore.documents)
const pagination = computed(() => documentStore.pagination)

const filteredDocuments = computed(() => {
  if (!searchQuery.value) return documents.value
  
  const query = searchQuery.value.toLowerCase()
  return documents.value.filter(doc => 
    doc.title.toLowerCase().includes(query) ||
    doc.content.toLowerCase().includes(query)
  )
})

const fetchDocuments = async () => {
  isLoading.value = true
  try {
    await documentStore.fetchDocuments({
      status: filterStatus.value || undefined
    })
  } catch (error) {
    console.error('Fetch documents error:', error)
  } finally {
    isLoading.value = false
  }
}

const handleSearch = () => {
  // 本地搜索，无需重新请求
}

const getPreview = (content) => {
  if (!content) return ''
  const text = content.replace(/[#*`>]/g, '').replace(/\n/g, ' ')
  return text.length > 100 ? text.substring(0, 100) + '...' : text
}

const formatTime = (time) => {
  return dayjs(time).format('YYYY-MM-DD HH:mm')
}

const goToCreate = () => {
  router.push('/documents/create')
}

const goToDetail = (id) => {
  router.push(`/documents/${id}`)
}

const handleCommand = async (command, document) => {
  switch (command) {
    case 'edit':
      router.push(`/documents/${document._id}/edit`)
      break
    case 'permissions':
      router.push(`/documents/${document._id}/permissions`)
      break
    case 'versions':
      router.push(`/documents/${document._id}/versions`)
      break
    case 'delete':
      handleDelete(document)
      break
  }
}

const handleDelete = async (document) => {
  try {
    await ElMessageBox.confirm(
      `确定要删除文档 "${document.title}" 吗？此操作不可恢复。`,
      '删除确认',
      {
        confirmButtonText: '确定删除',
        cancelButtonText: '取消',
        type: 'warning'
      }
    )
    
    await documentStore.deleteDocument(document._id)
    ElMessage.success('删除成功')
  } catch (error) {
    if (error !== 'cancel') {
      console.error('Delete document error:', error)
    }
  }
}

onMounted(() => {
  fetchDocuments()
})
</script>

<style lang="scss" scoped>
.document-list-page {
  padding: 24px;
  max-width: 1400px;
  margin: 0 auto;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 24px;
  flex-wrap: wrap;
  gap: 16px;

  .header-left {
    display: flex;
    align-items: center;
    gap: 16px;

    h2 {
      margin: 0;
      font-size: 24px;
      font-weight: 600;
      color: #303133;
    }
  }

  .header-right {
    display: flex;
    gap: 12px;
    align-items: center;
  }

  .search-input {
    width: 300px;
  }
}

.document-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 20px;
}

.document-card {
  position: relative;
  background: #fff;
  border-radius: 12px;
  padding: 20px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);
  cursor: pointer;
  transition: all 0.3s;
  overflow: hidden;

  &:hover {
    transform: translateY(-4px);
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.12);

    .card-hover-bg {
      opacity: 1;
    }
  }

  .card-header {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    margin-bottom: 12px;

    .doc-icon {
      font-size: 32px;
      color: #409EFF;
    }
  }

  .card-body {
    margin-bottom: 16px;

    .doc-title {
      margin: 0 0 8px 0;
      font-size: 18px;
      font-weight: 600;
      color: #303133;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }

    .doc-preview {
      margin: 0;
      font-size: 14px;
      color: #606266;
      line-height: 1.6;
      display: -webkit-box;
      -webkit-line-clamp: 2;
      -webkit-box-orient: vertical;
      overflow: hidden;
    }
  }

  .card-footer {
    display: flex;
    justify-content: space-between;
    align-items: center;

    .doc-tags {
      display: flex;
      gap: 6px;
      align-items: center;

      .more-tags {
        font-size: 12px;
        color: #909399;
      }
    }

    .doc-meta {
      display: flex;
      align-items: center;
      gap: 8px;

      .update-time {
        font-size: 12px;
        color: #909399;
      }
    }
  }

  .card-hover-bg {
    position: absolute;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background: linear-gradient(135deg, rgba(64, 158, 255, 0.1) 0%, rgba(103, 194, 58, 0.1) 100%);
    opacity: 0;
    transition: opacity 0.3s;
    pointer-events: none;
  }
}

.empty-state {
  grid-column: 1 / -1;
  padding: 60px 0;
}

.pagination {
  display: flex;
  justify-content: center;
  margin-top: 32px;
}

.loading-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(255, 255, 255, 0.8);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;

  .loading-icon {
    animation: rotate 1s linear infinite;
    color: #409EFF;
  }
}

@keyframes rotate {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}

@media (max-width: 768px) {
  .document-list-page {
    padding: 16px;
  }

  .page-header {
    flex-direction: column;
    align-items: stretch;

    .header-left {
      flex-direction: column;
      align-items: stretch;

      .search-input {
        width: 100%;
      }
    }

    .header-right {
      justify-content: space-between;

      .el-select {
        flex: 1;
      }
    }
  }

  .document-grid {
    grid-template-columns: 1fr;
  }
}
</style>
