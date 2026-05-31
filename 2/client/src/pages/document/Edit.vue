<template>
  <div class="document-edit-page">
    <div class="edit-header">
      <div class="header-left">
        <el-button text :icon="ArrowLeft" @click="goBack">
          返回
        </el-button>
        <el-input
          v-model="form.title"
          placeholder="输入文档标题..."
          class="title-input"
          size="large"
        />
      </div>
      <div class="header-right">
        <el-tag v-if="isEdit" type="info">v{{ document?.version }}</el-tag>
        <el-button :icon="View" @click="previewDocument">
          预览
        </el-button>
        <el-button
          type="primary"
          :icon="Check"
          :loading="isSaving"
          @click="handleSave"
        >
          保存
        </el-button>
      </div>
    </div>

    <div class="edit-layout">
      <div class="editor-container">
        <div class="editor-toolbar">
          <el-radio-group v-model="form.type" size="small" @change="handleTypeChange">
            <el-radio-button value="markdown">Markdown</el-radio-button>
            <el-radio-button value="txt">纯文本</el-radio-button>
          </el-radio-group>
          <div class="toolbar-divider" />
          <el-button
            v-if="form.type === 'markdown'"
            text
            size="small"
            :icon="Bold"
            @click="insertMarkdown('**', '**')"
          >
            粗体
          </el-button>
          <el-button
            v-if="form.type === 'markdown'"
            text
            size="small"
            :icon="Italic"
            @click="insertMarkdown('*', '*')"
          >
            斜体
          </el-button>
          <el-button
            v-if="form.type === 'markdown'"
            text
            size="small"
            :icon="DocumentAdd"
            @click="insertMarkdown('# ', '')"
          >
            标题
          </el-button>
          <el-button
            v-if="form.type === 'markdown'"
            text
            size="small"
            :icon="List"
            @click="insertMarkdown('\n- ', '')"
          >
            列表
          </el-button>
          <el-button
            v-if="form.type === 'markdown'"
            text
            size="small"
            :icon="Link"
            @click="insertMarkdown('[链接文本](', ')')"
          >
            链接
          </el-button>
          <el-button
            v-if="form.type === 'markdown'"
            text
            size="small"
            :icon="Picture"
            @click="insertMarkdown('![图片描述](', ')')"
          >
            图片
          </el-button>
          <el-button
            v-if="form.type === 'markdown'"
            text
            size="small"
            :icon="Code"
            @click="insertMarkdown('\n```\n', '\n```\n')"
          >
            代码
          </el-button>
        </div>

        <div class="editor-body">
          <el-input
            ref="editorRef"
            v-model="form.content"
            type="textarea"
            :autosize="{ minRows: 20, maxRows: 50 }"
            placeholder="开始编写文档内容..."
            resize="none"
            class="editor-textarea"
          />
        </div>
      </div>

      <div v-if="showPreview" class="preview-container">
        <div class="preview-header">
          <el-icon><View /></el-icon>
          <span>预览</span>
        </div>
        <div class="preview-body">
          <div
            v-if="form.type === 'markdown'"
            class="markdown-preview"
            v-html="renderedContent"
          />
          <pre v-else class="text-preview">{{ form.content }}</pre>
        </div>
      </div>
    </div>

    <div class="edit-footer">
      <div class="footer-left">
        <span class="word-count">字数: {{ wordCount }}</span>
        <span class="char-count">字符: {{ charCount }}</span>
      </div>
      <div class="footer-right">
        <el-tag
          :type="autoSaveStatus === 'saved' ? 'success' : autoSaveStatus === 'saving' ? 'warning' : 'info'"
          size="small"
        >
          {{ autoSaveText }}
        </el-tag>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, watch } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useDocumentStore } from '@/stores/document'
import { createDocument as apiCreate, updateDocument as apiUpdate } from '@/utils/api'
import { ElMessage, ElMessageBox } from 'element-plus'
import { ArrowLeft, Check, View, Bold, Italic, DocumentAdd, List, Link, Picture, Code } from '@element-plus/icons-vue'
import { marked } from 'marked'
import socketService from '@/utils/socket'

const router = useRouter()
const route = useRoute()
const documentStore = useDocumentStore()

const isEdit = computed(() => !!route.params.id)
const documentId = computed(() => route.params.id)

const editorRef = ref(null)
const isSaving = ref(false)
const showPreview = ref(true)
const autoSaveStatus = ref('idle')
let autoSaveTimer = null

const form = reactive({
  title: '',
  content: '',
  type: 'markdown',
  tags: []
})

const document = computed(() => documentStore.currentDocument)

const renderedContent = computed(() => {
  if (form.type === 'markdown') {
    return marked(form.content || '')
  }
  return form.content || ''
})

const wordCount = computed(() => {
  return form.content?.replace(/\s/g, '').length || 0
})

const charCount = computed(() => {
  return form.content?.length || 0
})

const autoSaveText = computed(() => {
  switch (autoSaveStatus.value) {
    case 'saving': return '保存中...'
    case 'saved': return '已自动保存'
    case 'conflict': return '存在冲突，请刷新'
    default: return '等待编辑'
  }
})

const goBack = () => {
  router.back()
}

const loadDocument = async () => {
  if (!isEdit.value) return

  try {
    await documentStore.fetchDocument(documentId.value)
    if (document.value) {
      form.title = document.value.title
      form.content = document.value.content
      form.type = document.value.type || 'markdown'
    }
  } catch (error) {
    console.error('Load document error:', error)
    ElMessage.error('加载文档失败')
  }
}

const handleSave = async () => {
  if (!form.title.trim()) {
    ElMessage.warning('请输入文档标题')
    return
  }

  if (!form.content.trim()) {
    ElMessage.warning('请输入文档内容')
    return
  }

  isSaving.value = true
  try {
    let response
    if (isEdit.value) {
      const currentVersion = document.value?.version
      response = await apiUpdate(documentId.value, form, currentVersion)
      ElMessage.success('保存成功')
      socketService.sendDocumentEdit({
        documentId: documentId.value,
        content: form.content,
        version: response.data?.version
      })
    } else {
      response = await apiCreate(form)
      ElMessage.success('创建成功')
      router.replace(`/documents/${response.data._id}/edit`)
    }
    autoSaveStatus.value = 'saved'
  } catch (error) {
    if (error.response?.status === 409) {
      const currentVersion = error.response.data?.currentVersion
      try {
        await ElMessageBox.confirm(
          `文档已被其他用户修改（当前版本v${currentVersion}），是否刷新数据后重试？`,
          '冲突提示',
          {
            confirmButtonText: '刷新并重试',
            cancelButtonText: '取消',
            type: 'warning'
          }
        )
        await loadDocument()
      } catch {
        // 用户取消
      }
    } else {
      console.error('Save document error:', error)
      ElMessage.error('保存失败')
    }
  } finally {
    isSaving.value = false
  }
}

const handleTypeChange = () => {
  // 类型变更时无需特殊处理
}

const insertMarkdown = (prefix, suffix) => {
  const textarea = editorRef.value?.textarea
  if (!textarea) return

  const start = textarea.selectionStart
  const end = textarea.selectionEnd
  const selectedText = form.content.substring(start, end)
  const beforeText = form.content.substring(0, start)
  const afterText = form.content.substring(end)

  form.content = beforeText + prefix + selectedText + suffix + afterText

  setTimeout(() => {
    textarea.focus()
    const newStart = start + prefix.length
    const newEnd = newStart + selectedText.length
    textarea.setSelectionRange(newStart, newEnd)
  }, 0)
}

const previewDocument = () => {
  showPreview.value = !showPreview.value
}

const autoSave = () => {
  if (autoSaveTimer) {
    clearTimeout(autoSaveTimer)
  }

  autoSaveStatus.value = 'saving'
  autoSaveTimer = setTimeout(async () => {
    if (isEdit.value && form.title && form.content) {
      try {
        const currentVersion = document.value?.version
        await apiUpdate(documentId.value, form, currentVersion)
        autoSaveStatus.value = 'saved'
      } catch (error) {
        if (error.response?.status === 409) {
          autoSaveStatus.value = 'conflict'
        } else {
          console.error('Auto save error:', error)
          autoSaveStatus.value = 'idle'
        }
      }
    }
  }, 3000)
}

watch(() => form.content, autoSave)
watch(() => form.title, autoSave)

onMounted(() => {
  loadDocument()
})
</script>

<style lang="scss" scoped>
.document-edit-page {
  display: flex;
  flex-direction: column;
  height: 100vh;
  background: #fff;
}

.edit-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 24px;
  border-bottom: 1px solid #ebeef5;

  .header-left {
    display: flex;
    align-items: center;
    gap: 16px;
    flex: 1;

    .title-input {
      flex: 1;
      max-width: 500px;

      :deep(.el-input__inner) {
        font-size: 18px;
        font-weight: 600;
        border: none;
        background: transparent;
      }
    }
  }

  .header-right {
    display: flex;
    align-items: center;
    gap: 12px;
  }
}

.edit-layout {
  flex: 1;
  display: flex;
  overflow: hidden;
}

.editor-container {
  flex: 1;
  display: flex;
  flex-direction: column;
  border-right: 1px solid #ebeef5;
}

.editor-toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 16px;
  border-bottom: 1px solid #ebeef5;
  background: #f5f7fa;

  .toolbar-divider {
    width: 1px;
    height: 20px;
    background: #dcdfe6;
    margin: 0 8px;
  }
}

.editor-body {
  flex: 1;
  padding: 16px;
  overflow-y: auto;

  .editor-textarea {
    :deep(.el-textarea__inner) {
      font-family: 'Monaco', 'Menlo', 'Consolas', monospace;
      font-size: 14px;
      line-height: 1.8;
      border: none;
      background: transparent;
      resize: none;
      box-shadow: none;
    }
  }
}

.preview-container {
  flex: 1;
  display: flex;
  flex-direction: column;
  background: #fafafa;

  .preview-header {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 12px 16px;
    border-bottom: 1px solid #ebeef5;
    font-weight: 500;
    color: #606266;
  }

  .preview-body {
    flex: 1;
    padding: 24px;
    overflow-y: auto;
  }

  .markdown-preview {
    line-height: 1.8;

    :deep(h1), :deep(h2), :deep(h3), :deep(h4), :deep(h5), :deep(h6) {
      margin-top: 24px;
      margin-bottom: 16px;
      font-weight: 600;
    }

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
  }

  .text-preview {
    white-space: pre-wrap;
    word-break: break-word;
    font-family: 'Monaco', 'Menlo', monospace;
    line-height: 1.8;
  }
}

.edit-footer {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 24px;
  border-top: 1px solid #ebeef5;
  font-size: 12px;
  color: #909399;

  .footer-left {
    display: flex;
    gap: 16px;
  }
}

@media (max-width: 768px) {
  .edit-header {
    padding: 8px 16px;

    .header-left {
      .title-input {
        :deep(.el-input__inner) {
          font-size: 16px;
        }
      }
    }
  }

  .edit-layout {
    flex-direction: column;
  }

  .editor-container {
    border-right: none;
    border-bottom: 1px solid #ebeef5;
  }

  .preview-container {
    max-height: 40%;
  }

  .edit-footer {
    padding: 8px 16px;
  }
}
</style>
