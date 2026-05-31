<template>
  <div class="document-detail-page">
    <div class="page-layout">
      <div class="main-content">
        <div class="doc-toolbar">
          <div class="toolbar-left">
            <el-button text :icon="ArrowLeft" @click="goBack">
              返回
            </el-button>
            <span class="doc-title">{{ document?.title }}</span>
          </div>
          <div class="toolbar-right">
            <el-button
              v-if="canEdit"
              type="primary"
              :icon="Edit"
              @click="goToEdit"
            >
              编辑
            </el-button>
            <el-button :icon="Share" @click="showShareDialog = true">
              分享
            </el-button>
            <el-dropdown trigger="click" @command="handleCommand">
              <el-button>
                更多
                <el-icon class="el-icon--right"><ArrowDown /></el-icon>
              </el-button>
              <template #dropdown>
                <el-dropdown-menu>
                  <el-dropdown-item command="permissions">
                    <el-icon><Lock /></el-icon>
                    权限管理
                  </el-dropdown-item>
                  <el-dropdown-item command="versions">
                    <el-icon><Clock /></el-icon>
                    历史版本
                  </el-dropdown-item>
                  <el-dropdown-item command="export-doc">
                    <el-icon><Download /></el-icon>
                    导出文档
                  </el-dropdown-item>
                  <el-dropdown-item divided command="export-annotations-json">
                    <el-icon><Document /></el-icon>
                    导出批注(JSON)
                  </el-dropdown-item>
                  <el-dropdown-item command="export-annotations-csv">
                    <el-icon><Document /></el-icon>
                    导出批注(CSV)
                  </el-dropdown-item>
                  <el-dropdown-item command="export-annotations-md">
                    <el-icon><Document /></el-icon>
                    导出批注(Markdown)
                  </el-dropdown-item>
                </el-dropdown-menu>
              </template>
            </el-dropdown>
          </div>
        </div>

        <div class="doc-content-area">
          <DocumentRender
            :document-id="documentId"
            :document="document"
            :allow-selection="canComment"
            @select-text="handleSelectText"
            @create-annotation="handleCreateAnnotation"
            @annotation-created="handleAnnotationCreated"
          />
        </div>
      </div>

      <div class="sidebar">
        <el-tabs v-model="activeTab" class="sidebar-tabs">
          <el-tab-pane label="批注" name="annotations">
            <div class="annotation-panel">
              <div class="panel-header">
                <div class="header-top">
                  <el-input
                    v-model="searchKeyword"
                    placeholder="搜索批注..."
                    :prefix-icon="Search"
                    clearable
                    size="small"
                    @input="handleSearchInput"
                    @clear="clearSearch"
                  />
                </div>
                <div class="header-bottom">
                  <el-radio-group v-model="annotationFilter" size="small" @change="fetchAnnotations">
                    <el-radio-button value="all">全部</el-radio-button>
                    <el-radio-button value="active">未解决</el-radio-button>
                    <el-radio-button value="resolved">已解决</el-radio-button>
                  </el-radio-group>
                  <el-dropdown trigger="click" @command="handleExportAnnotations">
                    <el-button size="small" :icon="Download">
                      导出
                    </el-button>
                    <template #dropdown>
                      <el-dropdown-menu>
                        <el-dropdown-item command="json">JSON</el-dropdown-item>
                        <el-dropdown-item command="csv">CSV</el-dropdown-item>
                        <el-dropdown-item command="markdown">Markdown</el-dropdown-item>
                      </el-dropdown-menu>
                    </template>
                  </el-dropdown>
                </div>
              </div>
              <div v-if="searchKeyword" class="search-info">
                <el-alert
                  type="info"
                  :closable="false"
                  show-icon
                  :title="`找到 ${filteredAnnotations.length} 条结果`"
                />
              </div>
              <div class="annotation-stats">
                <el-tag type="info">共 {{ annotationStats.total }} 条</el-tag>
                <el-tag type="warning">未解决 {{ annotationStats.active }}</el-tag>
                <el-tag type="success">已解决 {{ annotationStats.resolved }}</el-tag>
              </div>
              <div class="annotation-list">
                <div
                  v-for="annotation in filteredAnnotations"
                  :key="annotation._id"
                  class="annotation-item"
                  :class="{ resolved: annotation.status === 'resolved' }"
                >
                  <div class="annotation-header">
                    <el-avatar :size="28">
                      {{ getAuthorName(annotation.author).charAt(0).toUpperCase() }}
                    </el-avatar>
                    <div class="annotation-meta">
                      <span class="author">{{ getAuthorName(annotation.author) }}</span>
                      <span class="time">{{ formatTime(annotation.createdAt) }}</span>
                    </div>
                    <el-tag
                      v-if="annotation.status === 'resolved'"
                      type="success"
                      size="small"
                    >
                      已解决
                    </el-tag>
                  </div>
                  <div class="annotation-content">
                    <span
                      v-if="annotation.type === 'highlight'"
                      class="highlight-text"
                      :style="{ backgroundColor: annotation.color + '40' }"
                    >
                      {{ annotation.position?.selectionText }}
                    </span>
                    <p v-html="highlightKeyword(annotation.content)"></p>
                  </div>
                  <div class="annotation-actions">
                    <el-button
                      v-if="canResolve && annotation.status === 'active'"
                      text
                      size="small"
                      type="success"
                      @click="resolveAnnotation(annotation)"
                    >
                      <el-icon><Check /></el-icon>
                      解决
                    </el-button>
                    <el-button
                      v-if="canResolve && annotation.status === 'resolved'"
                      text
                      size="small"
                      @click="unresolveAnnotation(annotation)"
                    >
                      取消解决
                    </el-button>
                    <el-button
                      text
                      size="small"
                      @click="showReplyInput(annotation)"
                    >
                      <el-icon><ChatDotRound /></el-icon>
                      回复
                    </el-button>
                    <el-button
                      v-if="canDelete(annotation)"
                      text
                      size="small"
                      type="danger"
                      @click="deleteAnnotation(annotation)"
                    >
                      <el-icon><Delete /></el-icon>
                    </el-button>
                  </div>
                  <div v-if="annotation.replies?.length > 0" class="annotation-replies">
                    <div
                      v-for="reply in annotation.replies"
                      :key="reply._id"
                      class="reply-item"
                    >
                      <el-avatar :size="20">
                        {{ getAuthorName(reply.author).charAt(0).toUpperCase() }}
                      </el-avatar>
                      <div class="reply-content">
                        <span class="reply-author">{{ getAuthorName(reply.author) }}</span>
                        <p>{{ reply.content }}</p>
                      </div>
                    </div>
                  </div>
                </div>

                <el-empty
                  v-if="!isLoadingAnnotations && filteredAnnotations.length === 0"
                  :description="searchKeyword ? '未找到匹配的批注' : '暂无批注'"
                  :image-size="60"
                />
              </div>
            </div>
          </el-tab-pane>

          <el-tab-pane label="在线用户" name="users">
            <RealtimeSync
              :document-id="documentId"
              @sync="fetchAnnotations"
            />
          </el-tab-pane>

          <el-tab-pane label="历史版本" name="versions">
            <HistoryVersion
              :versions="versions"
              :current-version="document?.version"
              @restore="handleVersionRestored"
              @refresh="fetchVersions"
            />
          </el-tab-pane>
        </el-tabs>
      </div>
    </div>

    <el-dialog
      v-model="showShareDialog"
      title="分享文档"
      width="480px"
    >
      <div class="share-dialog">
        <el-form label-width="80px">
          <el-form-item label="分享链接">
            <el-input
              :model-value="shareLink"
              readonly
              :suffix-icon="CopyDocument"
              @click="copyShareLink"
            />
          </el-form-item>
          <el-form-item label="权限">
            <el-radio-group v-model="sharePermission">
              <el-radio value="view">仅查看</el-radio>
              <el-radio value="comment">可批注</el-radio>
            </el-radio-group>
          </el-form-item>
        </el-form>
      </div>
      <template #footer>
        <el-button @click="showShareDialog = false">关闭</el-button>
        <el-button type="primary" @click="copyShareLink">复制链接</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useUserStore } from '@/stores/user'
import { useDocumentStore } from '@/stores/document'
import { 
  getAnnotations, 
  getAnnotationStats, 
  resolveAnnotation as apiResolve, 
  unresolveAnnotation as apiUnresolve, 
  deleteAnnotation as apiDelete, 
  replyAnnotation,
  searchAnnotations as apiSearchAnnotations,
  exportAnnotations as apiExportAnnotations
} from '@/utils/api'
import { ElMessage, ElMessageBox } from 'element-plus'
import { ArrowLeft, Edit, Share, ArrowDown, Lock, Clock, Download, Check, ChatDotRound, Delete, CopyDocument, Search, Document } from '@element-plus/icons-vue'
import DocumentRender from '@components/DocumentRender/Index.vue'
import RealtimeSync from '@components/RealtimeSync/Index.vue'
import HistoryVersion from '@components/HistoryVersion/Index.vue'
import socketService from '@/utils/socket'
import dayjs from '@/utils/dayjs'

const router = useRouter()
const route = useRoute()
const userStore = useUserStore()
const documentStore = useDocumentStore()

const documentId = computed(() => route.params.id)
const document = computed(() => documentStore.currentDocument)
const versions = computed(() => documentStore.versions)

const activeTab = ref('annotations')
const annotationFilter = ref('all')
const searchKeyword = ref('')
const searchTimer = ref(null)
const annotations = ref([])
const annotationStats = ref({ total: 0, active: 0, resolved: 0 })
const isLoadingAnnotations = ref(false)
const showShareDialog = ref(false)
const sharePermission = ref('view')

const canEdit = computed(() => {
  if (!document.value) return false
  const ownerId = typeof document.value.owner === 'string' ? document.value.owner : document.value.owner?._id
  return ownerId === userStore.user?._id
})

const canComment = computed(() => {
  if (!document.value) return false
  if (canEdit.value) return true
  const collaborator = document.value.collaborators?.find(
    c => (typeof c.user === 'string' ? c.user : c.user?._id) === userStore.user?._id
  )
  return collaborator && ['comment', 'edit'].includes(collaborator.permission)
})

const canResolve = computed(() => canEdit.value)

const filteredAnnotations = computed(() => {
  let result = annotations.value
  
  if (searchKeyword.value.trim()) {
    const keyword = searchKeyword.value.toLowerCase()
    result = result.filter(a => 
      a.content?.toLowerCase().includes(keyword) ||
      a.position?.selectionText?.toLowerCase().includes(keyword)
    )
  }
  
  if (annotationFilter.value !== 'all') {
    result = result.filter(a => a.status === annotationFilter.value)
  }
  
  return result
})

const shareLink = computed(() => {
  return `${window.location.origin}/documents/${documentId.value}?permission=${sharePermission.value}`
})

const formatTime = (time) => {
  return dayjs(time).format('MM-DD HH:mm')
}

const getAuthorName = (author) => {
  if (!author) return '未知用户'
  return typeof author === 'string' ? '用户' : author.username || '未知用户'
}

const canDelete = (annotation) => {
  const authorId = typeof annotation.author === 'string' ? annotation.author : annotation.author?._id
  return authorId === userStore.user?._id || canEdit.value
}

const highlightKeyword = (text) => {
  if (!searchKeyword.value.trim() || !text) return text
  const keyword = searchKeyword.value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
  const regex = new RegExp(`(${keyword})`, 'gi')
  return text.replace(regex, '<mark class="highlight">$1</mark>')
}

const goBack = () => {
  router.back()
}

const goToEdit = () => {
  router.push(`/documents/${documentId.value}/edit`)
}

const handleCommand = (command) => {
  switch (command) {
    case 'permissions':
      router.push(`/documents/${documentId.value}/permissions`)
      break
    case 'versions':
      activeTab.value = 'versions'
      fetchVersions()
      break
    case 'export-doc':
      exportDocument()
      break
    case 'export-annotations-json':
      handleExportAnnotations('json')
      break
    case 'export-annotations-csv':
      handleExportAnnotations('csv')
      break
    case 'export-annotations-md':
      handleExportAnnotations('markdown')
      break
  }
}

const fetchDocument = async () => {
  try {
    await documentStore.fetchDocument(documentId.value)
  } catch (error) {
    console.error('Fetch document error:', error)
  }
}

const fetchAnnotations = async () => {
  if (searchKeyword.value.trim()) {
    handleSearchInput()
    return
  }
  
  isLoadingAnnotations.value = true
  try {
    const params = { status: 'all' }
    if (annotationFilter.value !== 'all') {
      params.status = annotationFilter.value
    }
    
    const [annotationsRes, statsRes] = await Promise.all([
      getAnnotations(documentId.value, params),
      getAnnotationStats(documentId.value)
    ])
    annotations.value = annotationsRes.data.annotations
    annotationStats.value = statsRes.data
  } catch (error) {
    console.error('Fetch annotations error:', error)
  } finally {
    isLoadingAnnotations.value = false
  }
}

const handleSearchInput = () => {
  if (searchTimer.value) {
    clearTimeout(searchTimer.value)
  }
  
  searchTimer.value = setTimeout(async () => {
    if (!searchKeyword.value.trim()) {
      clearSearch()
      return
    }
    
    isLoadingAnnotations.value = true
    try {
      const params = {
        keyword: searchKeyword.value.trim()
      }
      
      if (annotationFilter.value !== 'all') {
        params.status = annotationFilter.value
      }
      
      const response = await apiSearchAnnotations(documentId.value, params)
      annotations.value = response.data
      annotationStats.value.total = response.total
    } catch (error) {
      console.error('Search annotations error:', error)
    } finally {
      isLoadingAnnotations.value = false
    }
  }, 300)
}

const clearSearch = () => {
  searchKeyword.value = ''
  fetchAnnotations()
}

const fetchVersions = async () => {
  try {
    await documentStore.fetchVersions(documentId.value)
  } catch (error) {
    console.error('Fetch versions error:', error)
  }
}

const handleSelectText = (data) => {
  console.log('Selected text:', data)
}

const handleCreateAnnotation = (data) => {
  console.log('Create annotation:', data)
}

const handleAnnotationCreated = (annotation) => {
  annotations.value.unshift(annotation)
  annotationStats.value.total++
  annotationStats.value.active++
  socketService.sendAnnotationCreated({
    documentId: documentId.value,
    annotation
  })
}

const handleExportAnnotations = async (format) => {
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
    
    link.setAttribute('download', `annotations_${documentId.value}_${Date.now()}.${extensions[format]}`)
    document.body.appendChild(link)
    link.click()
    document.body.removeChild(link)
    window.URL.revokeObjectURL(url)
    
    ElMessage.success('导出成功')
  } catch (error) {
    console.error('Export annotations error:', error)
    ElMessage.error('导出失败')
  }
}

const resolveAnnotation = async (annotation) => {
  try {
    await apiResolve(annotation._id, annotation.version)
    annotation.status = 'resolved'
    annotationStats.value.active--
    annotationStats.value.resolved++
    ElMessage.success('批注已解决')
    socketService.sendAnnotationResolved({
      documentId: documentId.value,
      annotationId: annotation._id
    })
  } catch (error) {
    if (error.response?.status === 409) {
      const currentVersion = error.response.data?.currentVersion
      try {
        await ElMessageBox.confirm(
          `批注已被修改（当前版本v${currentVersion}），是否刷新后重试？`,
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

const unresolveAnnotation = async (annotation) => {
  try {
    await apiUnresolve(annotation._id)
    annotation.status = 'active'
    annotationStats.value.active++
    annotationStats.value.resolved--
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

const deleteAnnotation = async (annotation) => {
  try {
    await apiDelete(annotation._id, annotation.version)
    const index = annotations.value.findIndex(a => a._id === annotation._id)
    if (index > -1) {
      annotations.value.splice(index, 1)
      if (annotation.status === 'resolved') {
        annotationStats.value.resolved--
      } else {
        annotationStats.value.active--
      }
      annotationStats.value.total--
    }
    ElMessage.success('删除成功')
    socketService.sendAnnotationDeleted({
      documentId: documentId.value,
      annotationId: annotation._id
    })
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
      console.error('Delete annotation error:', error)
    }
  }
}

const showReplyInput = (annotation) => {
  // 实现回复功能
  console.log('Reply to annotation:', annotation)
}

const handleVersionRestored = (documentData) => {
  if (documentData) {
    documentStore.currentDocument = documentData
  }
  ElMessage.success('版本恢复成功')
}

const copyShareLink = () => {
  navigator.clipboard.writeText(shareLink.value)
  ElMessage.success('链接已复制到剪贴板')
}

const exportDocument = () => {
  if (!document.value) return
  const content = document.value.content
  const blob = new Blob([content], { type: 'text/markdown' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `${document.value.title}.md`
  a.click()
  URL.revokeObjectURL(url)
}

const handleSocketAnnotationCreated = (data) => {
  if (data.annotation) {
    const exists = annotations.value.some(a => a._id === data.annotation._id)
    if (!exists) {
      annotations.value.unshift(data.annotation)
      annotationStats.value.total++
      annotationStats.value.active++
    }
  }
}

const handleSocketAnnotationUpdated = (data) => {
  const index = annotations.value.findIndex(a => a._id === data.annotationId)
  if (index !== -1 && data.updates) {
    annotations.value[index] = { 
      ...annotations.value[index], 
      ...data.updates,
      version: data.version || annotations.value[index].version
    }
  }
}

const handleSocketAnnotationDeleted = (data) => {
  const index = annotations.value.findIndex(a => a._id === data.annotationId)
  if (index > -1) {
    const annotation = annotations.value[index]
    annotations.value.splice(index, 1)
    if (annotation.status === 'resolved') {
      annotationStats.value.resolved--
    } else {
      annotationStats.value.active--
    }
    annotationStats.value.total--
  }
}

const handleSocketAnnotationResolved = (data) => {
  const index = annotations.value.findIndex(a => a._id === data.annotationId)
  if (index !== -1) {
    if (annotations.value[index].status === 'active') {
      annotationStats.value.active--
      annotationStats.value.resolved++
    }
    annotations.value[index] = { 
      ...annotations.value[index], 
      status: 'resolved' 
    }
  }
}

onMounted(() => {
  fetchDocument()
  fetchAnnotations()
  
  const token = localStorage.getItem('token')
  if (token) {
    socketService.connect(token)
  }

  socketService.on('annotation-created', handleSocketAnnotationCreated)
  socketService.on('annotation-updated', handleSocketAnnotationUpdated)
  socketService.on('annotation-deleted', handleSocketAnnotationDeleted)
  socketService.on('annotation-resolved', handleSocketAnnotationResolved)
})

onUnmounted(() => {
  if (searchTimer.value) {
    clearTimeout(searchTimer.value)
  }
  socketService.off('annotation-created', handleSocketAnnotationCreated)
  socketService.off('annotation-updated', handleSocketAnnotationUpdated)
  socketService.off('annotation-deleted', handleSocketAnnotationDeleted)
  socketService.off('annotation-resolved', handleSocketAnnotationResolved)
  socketService.leaveDocument()
  documentStore.clearCurrentDocument()
})
</script>

<style lang="scss" scoped>
.document-detail-page {
  height: 100vh;
  overflow: hidden;
}

.page-layout {
  display: flex;
  height: 100%;
}

.main-content {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  background: #f5f7fa;
}

.doc-toolbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 24px;
  background: #fff;
  border-bottom: 1px solid #ebeef5;

  .toolbar-left {
    display: flex;
    align-items: center;
    gap: 16px;

    .doc-title {
      font-size: 18px;
      font-weight: 600;
      color: #303133;
    }
  }

  .toolbar-right {
    display: flex;
    gap: 8px;
  }
}

.doc-content-area {
  flex: 1;
  overflow-y: auto;
  padding: 24px;
}

.sidebar {
  width: 380px;
  background: #fff;
  border-left: 1px solid #ebeef5;
  display: flex;
  flex-direction: column;
}

.sidebar-tabs {
  height: 100%;

  :deep(.el-tabs__content) {
    height: calc(100% - 35px);
    overflow-y: auto;
  }

  :deep(.el-tab-pane) {
    height: 100%;
  }
}

.annotation-panel {
  padding: 16px;
  height: 100%;
  display: flex;
  flex-direction: column;
}

.panel-header {
  margin-bottom: 12px;
  
  .header-top {
    margin-bottom: 12px;
  }
  
  .header-bottom {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 8px;
  }
}

.search-info {
  margin-bottom: 12px;
}

.annotation-stats {
  display: flex;
  gap: 8px;
  margin-bottom: 16px;
}

.annotation-list {
  flex: 1;
  overflow-y: auto;
}

.annotation-item {
  padding: 16px;
  border-radius: 8px;
  background: #f5f7fa;
  margin-bottom: 12px;
  transition: background 0.2s;

  &:hover {
    background: #ebeef5;
  }

  &.resolved {
    opacity: 0.7;

    .annotation-content {
      text-decoration: line-through;
    }
  }

  .annotation-header {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 8px;

    .annotation-meta {
      flex: 1;

      .author {
        font-weight: 500;
        color: #303133;
        font-size: 14px;
      }

      .time {
        display: block;
        font-size: 12px;
        color: #909399;
      }
    }
  }

  .annotation-content {
    margin-bottom: 8px;

    .highlight-text {
      padding: 2px 4px;
      border-radius: 3px;
      font-size: 12px;
      color: #606266;
      display: block;
      margin-bottom: 4px;
    }

    p {
      margin: 0;
      font-size: 14px;
      color: #303133;
      line-height: 1.6;
    }
  }

  .annotation-actions {
    display: flex;
    gap: 8px;
  }

  .annotation-replies {
    margin-top: 12px;
    padding-top: 12px;
    border-top: 1px solid #ebeef5;

    .reply-item {
      display: flex;
      gap: 8px;
      padding: 8px;
      background: #fff;
      border-radius: 6px;
      margin-bottom: 8px;

      .reply-content {
        flex: 1;

        .reply-author {
          font-size: 12px;
          font-weight: 500;
          color: #303133;
        }

        p {
          margin: 4px 0 0 0;
          font-size: 13px;
          color: #606266;
        }
      }
    }
  }
}

:deep(.highlight) {
  background-color: #fef08a;
  color: #854d0e;
  padding: 0 2px;
  border-radius: 2px;
}

.share-dialog {
  padding: 16px 0;
}

@media (max-width: 768px) {
  .page-layout {
    flex-direction: column;
  }

  .sidebar {
    width: 100%;
    height: 40%;
    border-left: none;
    border-top: 1px solid #ebeef5;
  }

  .doc-toolbar {
    padding: 8px 16px;

    .toolbar-left {
      .doc-title {
        font-size: 16px;
      }
    }

    .toolbar-right {
      .el-button {
        padding: 8px 12px;
      }
    }
  }

  .doc-content-area {
    padding: 16px;
  }
}
</style>
