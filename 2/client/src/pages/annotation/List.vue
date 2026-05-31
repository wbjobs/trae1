<template>
  <div class="annotation-page">
    <div class="page-header">
      <el-button text :icon="ArrowLeft" @click="goBack">
        返回
      </el-button>
      <h2>批注列表</h2>
      <span class="doc-title">{{ document?.title }}</span>
    </div>

    <div class="page-content">
      <div class="filter-bar">
        <div class="search-box">
          <el-input
            v-model="searchKeyword"
            placeholder="搜索批注内容..."
            :prefix-icon="Search"
            clearable
            @keyup.enter="handleSearch"
            @clear="handleSearch"
          >
            <template #append>
              <el-button :icon="Search" @click="handleSearch" />
            </template>
          </el-input>
        </div>
        <el-radio-group v-model="filterStatus" size="default" @change="fetchAnnotations">
          <el-radio-button value="all">全部</el-radio-button>
          <el-radio-button value="active">未解决</el-radio-button>
          <el-radio-button value="resolved">已解决</el-radio-button>
        </el-radio-group>
        <el-radio-group v-model="filterType" size="default" @change="fetchAnnotations">
          <el-radio-button value="all">所有类型</el-radio-button>
          <el-radio-button value="comment">评论</el-radio-button>
          <el-radio-button value="highlight">高亮</el-radio-button>
          <el-radio-button value="suggestion">建议</el-radio-button>
        </el-radio-group>
        <el-dropdown trigger="click" @command="handleExport">
          <el-button type="primary" :icon="Download">
            导出
          </el-button>
          <template #dropdown>
            <el-dropdown-menu>
              <el-dropdown-item command="json">导出为 JSON</el-dropdown-item>
              <el-dropdown-item command="csv">导出为 CSV</el-dropdown-item>
              <el-dropdown-item command="markdown">导出为 Markdown</el-dropdown-item>
            </el-dropdown-menu>
          </template>
        </el-dropdown>
        <div class="stats">
          <el-tag type="info">共 {{ stats.total }} 条</el-tag>
          <el-tag type="warning">未解决 {{ stats.active }}</el-tag>
          <el-tag type="success">已解决 {{ stats.resolved }}</el-tag>
        </div>
      </div>

      <div v-if="searchKeyword" class="search-info">
        <el-alert
          type="info"
          :closable="false"
          show-icon
        >
          <template #title>
            搜索 "{{ searchKeyword }}" 找到 {{ annotations.length }} 条结果
            <el-button link type="primary" @click="clearSearch">清除搜索</el-button>
          </template>
        </el-alert>
      </div>

      <div class="annotation-list">
        <div
          v-for="annotation in annotations"
          :key="annotation._id"
          class="annotation-card"
          :class="{ resolved: annotation.status === 'resolved' }"
        >
          <div class="card-header">
            <el-avatar :size="32">
              {{ getAuthorName(annotation.author).charAt(0).toUpperCase() }}
            </el-avatar>
            <div class="header-info">
              <span class="author">{{ getAuthorName(annotation.author) }}</span>
              <span class="time">{{ formatTime(annotation.createdAt) }}</span>
            </div>
            <el-tag :type="getTypeTag(annotation.type)" size="small">
              {{ getTypeLabel(annotation.type) }}
            </el-tag>
            <el-tag
              v-if="annotation.status === 'resolved'"
              type="success"
              size="small"
            >
              已解决
            </el-tag>
          </div>

          <div class="card-body">
            <div v-if="annotation.position?.selectionText" class="selection-text">
              <el-icon><Document /></el-icon>
              <span>"{{ annotation.position.selectionText }}"</span>
            </div>
            <p class="content" v-html="highlightKeyword(annotation.content)"></p>
          </div>

          <div class="card-footer">
            <el-button
              v-if="canResolve && annotation.status === 'active'"
              type="success"
              size="small"
              @click="handleResolve(annotation)"
            >
              解决
            </el-button>
            <el-button
              v-if="canResolve && annotation.status === 'resolved'"
              size="small"
              @click="handleUnresolve(annotation)"
            >
              取消解决
            </el-button>
            <el-button
              v-if="canDelete(annotation)"
              type="danger"
              size="small"
              @click="handleDelete(annotation)"
            >
              删除
            </el-button>
          </div>
        </div>

        <el-empty
          v-if="!isLoading && annotations.length === 0"
          :description="searchKeyword ? '未找到匹配的批注' : '暂无批注'"
          :image-size="100"
        />
      </div>

      <div v-if="pagination.totalPages > 1 && !searchKeyword" class="pagination">
        <el-pagination
          v-model:current-page="pagination.page"
          :page-size="pagination.limit"
          :total="pagination.total"
          layout="prev, pager, next"
          @current-change="fetchAnnotations"
        />
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useDocumentStore } from '@/stores/document'
import { useUserStore } from '@/stores/user'
import { 
  getAnnotations, 
  getAnnotationStats, 
  resolveAnnotation as apiResolve, 
  unresolveAnnotation as apiUnresolve, 
  deleteAnnotation as apiDelete,
  searchAnnotations as apiSearchAnnotations,
  exportAnnotations as apiExportAnnotations
} from '@/utils/api'
import { ElMessage, ElMessageBox } from 'element-plus'
import { ArrowLeft, Document, Search, Download } from '@element-plus/icons-vue'
import dayjs from '@/utils/dayjs'

const router = useRouter()
const route = useRoute()
const documentStore = useDocumentStore()
const userStore = useUserStore()

const documentId = computed(() => route.params.id)
const document = computed(() => documentStore.currentDocument)

const filterStatus = ref('all')
const filterType = ref('all')
const searchKeyword = ref('')
const annotations = ref([])
const stats = ref({ total: 0, active: 0, resolved: 0 })
const isLoading = ref(false)
const isExporting = ref(false)
const pagination = ref({
  page: 1,
  limit: 20,
  total: 0,
  totalPages: 0
})

const canResolve = computed(() => {
  if (!document.value) return false
  const ownerId = typeof document.value.owner === 'string' ? document.value.owner : document.value.owner?._id
  return ownerId === userStore.user?._id
})

const canDelete = (annotation) => {
  const authorId = typeof annotation.author === 'string' ? annotation.author : annotation.author?._id
  return authorId === userStore.user?._id || canResolve.value
}

const goBack = () => {
  router.back()
}

const formatTime = (time) => {
  return dayjs(time).format('YYYY-MM-DD HH:mm')
}

const getAuthorName = (author) => {
  if (!author) return '未知用户'
  return typeof author === 'string' ? '用户' : author.username || '未知用户'
}

const getTypeLabel = (type) => {
  const labels = {
    comment: '评论',
    highlight: '高亮',
    'sticky-note': '便签',
    underline: '下划线',
    suggestion: '建议'
  }
  return labels[type] || type
}

const getTypeTag = (type) => {
  const tags = {
    comment: '',
    highlight: 'warning',
    'sticky-note': 'info',
    underline: 'success',
    suggestion: 'primary'
  }
  return tags[type] || ''
}

const highlightKeyword = (text) => {
  if (!searchKeyword.value || !text) return text
  const regex = new RegExp(`(${searchKeyword.value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')})`, 'gi')
  return text.replace(regex, '<mark class="highlight">$1</mark>')
}

const clearSearch = () => {
  searchKeyword.value = ''
  fetchAnnotations()
}

const handleSearch = async () => {
  if (!searchKeyword.value.trim()) {
    clearSearch()
    return
  }
  
  isLoading.value = true
  try {
    const params = {
      keyword: searchKeyword.value.trim()
    }
    
    if (filterStatus.value !== 'all') {
      params.status = filterStatus.value
    }
    
    if (filterType.value !== 'all') {
      params.type = filterType.value
    }
    
    const response = await apiSearchAnnotations(documentId.value, params)
    annotations.value = response.data
    stats.value.total = response.total
  } catch (error) {
    console.error('Search annotations error:', error)
    ElMessage.error('搜索失败')
  } finally {
    isLoading.value = false
  }
}

const fetchAnnotations = async () => {
  if (searchKeyword.value) {
    handleSearch()
    return
  }
  
  isLoading.value = true
  try {
    const params = {
      page: pagination.value.page,
      limit: pagination.value.limit
    }

    if (filterStatus.value !== 'all') {
      params.status = filterStatus.value
    }

    const [annotationsRes, statsRes] = await Promise.all([
      getAnnotations(documentId.value, params),
      getAnnotationStats(documentId.value)
    ])

    let filteredAnnotations = annotationsRes.data.annotations
    
    if (filterType.value !== 'all') {
      filteredAnnotations = filteredAnnotations.filter(a => a.type === filterType.value)
    }

    annotations.value = filteredAnnotations
    stats.value = statsRes.data
    pagination.value = {
      ...pagination.value,
      total: annotationsRes.data.total,
      totalPages: annotationsRes.data.totalPages
    }
  } catch (error) {
    console.error('Fetch annotations error:', error)
  } finally {
    isLoading.value = false
  }
}

const handleExport = async (format) => {
  isExporting.value = true
  try {
    const params = {
      format,
      includeResolved: true
    }
    
    const blob = await apiExportAnnotations(documentId.value, params)
    
    const url = window.URL.createObjectURL(new Blob([blob]))
    const link = document.createElement('a')
    link.href = url
    
    const extensions = {
      json: 'json',
      csv: 'csv',
      markdown: 'md'
    }
    
    const contentDisposition = blob.type?.includes('json') ? 'application/json' : 
                              blob.type?.includes('csv') ? 'text/csv' : 'text/markdown'
    
    link.setAttribute('download', `annotations_${documentId.value}_${Date.now()}.${extensions[format]}`)
    document.body.appendChild(link)
    link.click()
    document.body.removeChild(link)
    window.URL.revokeObjectURL(url)
    
    ElMessage.success('导出成功')
  } catch (error) {
    console.error('Export annotations error:', error)
    ElMessage.error('导出失败')
  } finally {
    isExporting.value = false
  }
}

const handleResolve = async (annotation) => {
  try {
    await apiResolve(annotation._id, annotation.version)
    annotation.status = 'resolved'
    stats.value.active--
    stats.value.resolved++
    ElMessage.success('批注已解决')
  } catch (error) {
    if (error.response?.status === 409) {
      try {
        await ElMessageBox.confirm(
          '批注已被修改，是否刷新后重试？',
          '冲突提示',
          {
            confirmButtonText: '刷新',
            cancelButtonText: '取消',
            type: 'warning'
          }
        )
        await fetchAnnotations()
      } catch {
        // 用户取消
      }
    } else {
      console.error('Resolve annotation error:', error)
    }
  }
}

const handleUnresolve = async (annotation) => {
  try {
    await apiUnresolve(annotation._id)
    annotation.status = 'active'
    stats.value.active++
    stats.value.resolved--
    ElMessage.success('已取消解决')
  } catch (error) {
    if (error.response?.status === 409) {
      try {
        await ElMessageBox.confirm(
          '批注已被修改，是否刷新后重试？',
          '冲突提示',
          {
            confirmButtonText: '刷新',
            cancelButtonText: '取消',
            type: 'warning'
          }
        )
        await fetchAnnotations()
      } catch {
        // 用户取消
      }
    } else {
      console.error('Unresolve annotation error:', error)
    }
  }
}

const handleDelete = async (annotation) => {
  try {
    await ElMessageBox.confirm(
      '确定要删除这条批注吗？',
      '删除确认',
      {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning'
      }
    )

    await apiDelete(annotation._id, annotation.version)
    const index = annotations.value.findIndex(a => a._id === annotation._id)
    if (index > -1) {
      annotations.value.splice(index, 1)
      if (annotation.status === 'resolved') {
        stats.value.resolved--
      } else {
        stats.value.active--
      }
      stats.value.total--
    }
    ElMessage.success('删除成功')
  } catch (error) {
    if (error !== 'cancel') {
      if (error.response?.status === 409) {
        try {
          await ElMessageBox.confirm(
            '批注已被修改，是否刷新后重试？',
            '冲突提示',
            {
              confirmButtonText: '刷新',
              cancelButtonText: '取消',
              type: 'warning'
            }
          )
          await fetchAnnotations()
        } catch {
          // 用户取消
        }
      } else {
        console.error('Delete annotation error:', error)
      }
    }
  }
}

onMounted(() => {
  documentStore.fetchDocument(documentId.value)
  fetchAnnotations()
})
</script>

<style lang="scss" scoped>
.annotation-page {
  padding: 24px;
  max-width: 1000px;
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
  padding: 24px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);
}

.filter-bar {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-bottom: 16px;
  flex-wrap: wrap;

  .search-box {
    width: 280px;
  }

  .stats {
    display: flex;
    gap: 8px;
    margin-left: auto;
  }
}

.search-info {
  margin-bottom: 16px;
}

.annotation-list {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.annotation-card {
  padding: 16px;
  border-radius: 8px;
  border: 1px solid #ebeef5;
  transition: all 0.2s;

  &:hover {
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
  }

  &.resolved {
    opacity: 0.7;
    background: #f5f7fa;

    .content {
      text-decoration: line-through;
    }
  }

  .card-header {
    display: flex;
    align-items: center;
    gap: 12px;
    margin-bottom: 12px;

    .header-info {
      flex: 1;

      .author {
        font-weight: 500;
        color: #303133;
      }

      .time {
        display: block;
        font-size: 12px;
        color: #909399;
      }
    }
  }

  .card-body {
    margin-bottom: 12px;

    .selection-text {
      display: flex;
      align-items: center;
      gap: 8px;
      padding: 8px 12px;
      background: #f5f7fa;
      border-radius: 4px;
      font-size: 13px;
      color: #606266;
      margin-bottom: 8px;
      font-style: italic;
    }

    .content {
      margin: 0;
      font-size: 14px;
      color: #303133;
      line-height: 1.6;
    }
  }

  .card-footer {
    display: flex;
    gap: 8px;
    justify-content: flex-end;
  }
}

:deep(.highlight) {
  background-color: #fef08a;
  color: #854d0e;
  padding: 0 2px;
  border-radius: 2px;
}

.pagination {
  display: flex;
  justify-content: center;
  margin-top: 24px;
}

@media (max-width: 768px) {
  .annotation-page {
    padding: 16px;
  }

  .page-header {
    flex-wrap: wrap;

    h2 {
      font-size: 18px;
    }
  }

  .page-content {
    padding: 16px;
  }

  .filter-bar {
    flex-direction: column;
    align-items: stretch;

    .search-box {
      width: 100%;
    }

    .stats {
      margin-left: 0;
      justify-content: center;
    }
  }
}
</style>
