<template>
  <div class="document-render" :class="{ 'is-mobile': isMobile }">
    <div class="document-header">
      <h2 class="document-title">{{ document?.title }}</h2>
      <div class="document-meta">
        <el-tag v-if="document?.status === 'draft'" type="info">草稿</el-tag>
        <el-tag v-else-if="document?.status === 'published'" type="success">已发布</el-tag>
        <el-tag v-else type="warning">已归档</el-tag>
        <span class="meta-item">版本: v{{ document?.version }}</span>
        <span class="meta-item">更新于: {{ formatTime(document?.updatedAt) }}</span>
      </div>
    </div>

    <div class="document-content-wrapper">
      <div 
        ref="contentRef"
        class="document-content"
        :class="{ 'selectable': allowSelection }"
        @mouseup="handleSelection"
        @touchend="handleSelection"
      >
        <div v-if="document?.type === 'markdown'" class="markdown-content" v-html="renderedContent"></div>
        <pre v-else-if="document?.type === 'txt'" class="text-content">{{ document?.content }}</pre>
        <div v-else class="rich-content" v-html="document?.content"></div>
      </div>

      <div 
        v-if="showSelectionToolbar && selectedText"
        class="selection-toolbar"
        :style="toolbarStyle"
      >
        <el-button
          v-for="tool in toolbarTools"
          :key="tool.action"
          :type="tool.type || 'primary'"
          size="small"
          @click="handleToolClick(tool.action)"
        >
          <el-icon><component :is="tool.icon" /></el-icon>
          <span>{{ tool.label }}</span>
        </el-button>
      </div>
    </div>

    <div 
      v-if="showCreateAnnotation"
      class="annotation-popup"
      :style="popupStyle"
      @click.stop
    >
      <AnnotationEditor
        :document-id="documentId"
        :position="selectionPosition"
        :selected-text="selectedText"
        @close="showCreateAnnotation = false"
        @created="handleAnnotationCreated"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, nextTick } from 'vue'
import { marked } from 'marked'
import hljs from 'highlight.js'
import 'highlight.js/styles/github.css'
import AnnotationEditor from '@components/AnnotationEditor/Index.vue'
import { useUserStore } from '@/stores/user'
import dayjs from '@/utils/dayjs'

const props = defineProps({
  documentId: String,
  document: Object,
  allowSelection: {
    type: Boolean,
    default: true
  }
})

const emit = defineEmits(['select-text', 'create-annotation', 'annotation-created'])

const userStore = useUserStore()
const contentRef = ref(null)
const selectedText = ref('')
const selectionPosition = ref({ start: 0, end: 0 })
const showSelectionToolbar = ref(false)
const showCreateAnnotation = ref(false)
const toolbarPosition = ref({ x: 0, y: 0 })
const popupPosition = ref({ x: 0, y: 0 })
const isMobile = ref(false)

marked.setOptions({
  highlight: (code, lang) => {
    if (lang && hljs.getLanguage(lang)) {
      return hljs.highlight(code, { language: lang }).value
    }
    return hljs.highlightAuto(code).value
  },
  breaks: true,
  gfm: true
})

const renderedContent = computed(() => {
  if (props.document?.type === 'markdown') {
    return marked(props.document.content || '')
  }
  return props.document?.content || ''
})

const toolbarTools = [
  { action: 'highlight', label: '高亮', icon: 'EditPen', type: 'warning' },
  { action: 'comment', label: '批注', icon: 'ChatDotRound', type: 'primary' },
  { action: 'underline', label: '下划线', icon: 'Edit', type: 'success' }
]

const toolbarStyle = computed(() => ({
  left: `${toolbarPosition.value.x}px`,
  top: `${toolbarPosition.value.y}px`
}))

const popupStyle = computed(() => ({
  left: `${popupPosition.value.x}px`,
  top: `${popupPosition.value.y}px`
}))

const formatTime = (time) => {
  return dayjs(time).format('YYYY-MM-DD HH:mm')
}

const checkMobile = () => {
  isMobile.value = window.innerWidth < 768
}

const handleSelection = async () => {
  if (!props.allowSelection) return
  
  const selection = window.getSelection()
  if (!selection || selection.isCollapsed || !selection.toString().trim()) {
    selectedText.value = ''
    showSelectionToolbar.value = false
    return
  }

  const range = selection.getRangeAt(0)
  const rect = range.getBoundingClientRect()
  const containerRect = contentRef.value.getBoundingClientRect()

  selectedText.value = selection.toString()
  selectionPosition.value = {
    start: range.startOffset,
    end: range.endOffset,
    selectionText: selectedText.value
  }

  toolbarPosition.value = {
    x: rect.left - containerRect.left + rect.width / 2 - 100,
    y: rect.top - containerRect.top - 45
  }

  popupPosition.value = {
    x: rect.left - containerRect.left,
    y: rect.bottom - containerRect.top + 10
  }

  showSelectionToolbar.value = true
  emit('select-text', { text: selectedText.value, position: selectionPosition.value })
}

const handleToolClick = (action) => {
  showSelectionToolbar.value = false
  showCreateAnnotation.value = true
}

const handleAnnotationCreated = (annotation) => {
  showCreateAnnotation.value = false
  emit('annotation-created', annotation)
}

watch(() => props.documentId, () => {
  selectedText.value = ''
  showSelectionToolbar.value = false
  showCreateAnnotation.value = false
})

onMounted(() => {
  checkMobile()
  window.addEventListener('resize', checkMobile)
  document.addEventListener('click', (e) => {
    if (!e.target.closest('.selection-toolbar') && !e.target.closest('.annotation-popup')) {
      showSelectionToolbar.value = false
    }
  })
})
</script>

<style lang="scss" scoped>
.document-render {
  background: #fff;
  border-radius: 8px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);
  overflow: hidden;

  &.is-mobile {
    border-radius: 0;
    box-shadow: none;
  }
}

.document-header {
  padding: 24px;
  border-bottom: 1px solid #ebeef5;

  .document-title {
    margin: 0 0 12px 0;
    font-size: 24px;
    font-weight: 600;
    color: #303133;
  }

  .document-meta {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 12px;

    .meta-item {
      font-size: 13px;
      color: #909399;
    }
  }
}

.document-content-wrapper {
  position: relative;
  padding: 24px;
}

.document-content {
  line-height: 1.8;
  font-size: 14px;
  color: #303133;

  &.selectable {
    user-select: text;
  }

  :deep(h1), :deep(h2), :deep(h3), :deep(h4), :deep(h5), :deep(h6) {
    margin-top: 24px;
    margin-bottom: 16px;
    font-weight: 600;
  }

  :deep(h1) { font-size: 28px; }
  :deep(h2) { font-size: 24px; }
  :deep(h3) { font-size: 20px; }
  :deep(h4) { font-size: 18px; }

  :deep(p) {
    margin-bottom: 16px;
  }

  :deep(pre) {
    background: #f5f7fa;
    padding: 16px;
    border-radius: 4px;
    overflow-x: auto;
  }

  :deep(code) {
    background: #f5f7fa;
    padding: 2px 6px;
    border-radius: 3px;
    font-family: 'Monaco', 'Menlo', monospace;
  }

  :deep(blockquote) {
    border-left: 4px solid #dcdfe6;
    padding-left: 16px;
    color: #606266;
    margin: 16px 0;
  }

  :deep(table) {
    width: 100%;
    border-collapse: collapse;
    margin: 16px 0;

    th, td {
      border: 1px solid #ebeef5;
      padding: 8px 12px;
      text-align: left;
    }

    th {
      background: #f5f7fa;
      font-weight: 600;
    }
  }
}

.selection-toolbar {
  position: absolute;
  display: flex;
  gap: 8px;
  padding: 4px;
  background: #fff;
  border-radius: 6px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
  z-index: 100;

  .el-button {
    padding: 4px 8px;
  }
}

.annotation-popup {
  position: absolute;
  z-index: 200;
  min-width: 320px;
  background: #fff;
  border-radius: 8px;
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.15);
}

.markdown-content {
  word-break: break-word;
}

.text-content {
  white-space: pre-wrap;
  word-break: break-word;
  font-family: 'Monaco', 'Menlo', monospace;
  background: #f5f7fa;
  padding: 16px;
  border-radius: 4px;
}

.rich-content {
  word-break: break-word;
}

@media (max-width: 768px) {
  .document-header {
    padding: 16px;

    .document-title {
      font-size: 20px;
    }
  }

  .document-content-wrapper {
    padding: 16px;
  }

  .document-content {
    font-size: 15px;
  }

  .annotation-popup {
    min-width: auto;
    width: calc(100% - 32px);
    left: 16px !important;
  }
}
</style>
