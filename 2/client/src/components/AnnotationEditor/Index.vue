<template>
  <div class="annotation-editor">
    <div class="editor-header">
      <h4>{{ isEdit ? '编辑批注' : '添加批注' }}</h4>
      <el-button text size="small" @click="$emit('close')">
        <el-icon><Close /></el-icon>
      </el-button>
    </div>

    <div class="editor-body">
      <div v-if="selectedText" class="selected-text-preview">
        <el-icon><Document /></el-icon>
        <span class="text">"{{ selectedText }}"</span>
      </div>

      <div class="type-selector">
        <span class="label">批注类型：</span>
        <el-radio-group v-model="form.type" size="small">
          <el-radio-button 
            v-for="type in annotationTypes" 
            :key="type.value"
            :value="type.value"
          >
            <el-icon><component :is="type.icon" /></el-icon>
            <span class="type-label">{{ type.label }}</span>
          </el-radio-button>
        </el-radio-group>
      </div>

      <div class="visibility-selector">
        <span class="label">可见范围：</span>
        <el-radio-group v-model="form.visibility" size="small">
          <el-radio-button value="public">公开</el-radio-button>
          <el-radio-button value="private">仅自己</el-radio-button>
          <el-radio-button value="selected" :disabled="!canSelectUsers">指定人员</el-radio-button>
        </el-radio-group>
      </div>

      <div v-if="form.visibility === 'selected' && canSelectUsers" class="user-selector">
        <el-select
          v-model="form.visibleTo"
          multiple
          filterable
          placeholder="选择可见用户"
          size="small"
          style="width: 100%"
        >
          <el-option
            v-for="user in collaborators"
            :key="user._id"
            :label="user.username"
            :value="user._id"
          />
        </el-select>
      </div>

      <div class="color-selector">
        <span class="label">高亮颜色：</span>
        <div class="color-options">
          <div
            v-for="color in colors"
            :key="color"
            class="color-option"
            :class="{ active: form.color === color }"
            :style="{ backgroundColor: color }"
            @click="form.color = color"
          />
        </div>
      </div>

      <div class="content-editor">
        <el-input
          v-model="form.content"
          type="textarea"
          :rows="4"
          placeholder="输入批注内容..."
          maxlength="2000"
          show-word-limit
          resize="none"
        />
      </div>

      <div v-if="form.type === 'suggestion'" class="suggestion-input">
        <span class="label">建议修改：</span>
        <el-input
          v-model="form.suggestion"
          placeholder="输入建议替换的内容"
          size="small"
        />
      </div>
    </div>

    <div class="editor-footer">
      <el-button size="small" @click="$emit('close')">取消</el-button>
      <el-button
        type="primary"
        size="small"
        :loading="isSubmitting"
        :disabled="!canSubmit"
        @click="handleSubmit"
      >
        {{ isEdit ? '保存修改' : '提交批注' }}
      </el-button>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, watch } from 'vue'
import { createAnnotation, updateAnnotation } from '@/utils/api'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useUserStore } from '@/stores/user'
import socketService from '@/utils/socket'

const props = defineProps({
  documentId: String,
  position: Object,
  selectedText: String,
  annotation: Object,
  collaborators: {
    type: Array,
    default: () => []
  },
  canSelectUsers: {
    type: Boolean,
    default: true
  }
})

const emit = defineEmits(['close', 'created', 'updated', 'refresh-needed'])

const userStore = useUserStore()
const isSubmitting = ref(false)

const annotationTypes = [
  { value: 'highlight', label: '高亮', icon: 'EditPen' },
  { value: 'comment', label: '批注', icon: 'ChatDotRound' },
  { value: 'underline', label: '下划线', icon: 'Edit' },
  { value: 'sticky-note', label: '便签', icon: 'Postcard' },
  { value: 'suggestion', label: '建议', icon: 'Promotion' }
]

const colors = [
  '#FFEB3B',
  '#8BC34A',
  '#00BCD4',
  '#FF9800',
  '#E91E63',
  '#9C27B0'
]

const isEdit = computed(() => !!props.annotation)

const form = reactive({
  type: 'comment',
  content: '',
  visibility: 'public',
  visibleTo: [],
  color: '#FFEB3B',
  suggestion: ''
})

const canSubmit = computed(() => {
  return form.content.trim().length > 0
})

watch(() => props.annotation, (val) => {
  if (val) {
    form.type = val.type || 'comment'
    form.content = val.content || ''
    form.visibility = val.visibility || 'public'
    form.visibleTo = val.visibleTo?.map(u => typeof u === 'string' ? u : u._id) || []
    form.color = val.color || '#FFEB3B'
  } else {
    form.type = 'comment'
    form.content = ''
    form.visibility = 'public'
    form.visibleTo = []
    form.color = '#FFEB3B'
    form.suggestion = ''
  }
}, { immediate: true })

const handleSubmit = async () => {
  if (!canSubmit.value) return

  isSubmitting.value = true
  try {
    const data = {
      documentId: props.documentId,
      type: form.type,
      content: form.content.trim(),
      visibility: form.visibility,
      color: form.color,
      position: props.position
    }

    if (form.visibility === 'selected') {
      data.visibleTo = form.visibleTo
    }

    if (form.type === 'suggestion' && form.suggestion) {
      data.suggestion = form.suggestion
    }

    let response
    if (isEdit.value) {
      const currentVersion = props.annotation?.version
      response = await updateAnnotation(props.annotation._id, data, currentVersion)
      emit('updated', response.data)
      ElMessage.success('批注更新成功')
      socketService.sendAnnotationUpdated({
        documentId: props.documentId,
        annotationId: props.annotation._id,
        updates: data,
        version: response.data?.version
      })
    } else {
      response = await createAnnotation(data)
      emit('created', response.data)
      ElMessage.success('批注创建成功')
    }
  } catch (error) {
    if (error.response?.status === 409) {
      const currentVersion = error.response.data?.currentVersion
      try {
        await ElMessageBox.confirm(
          `批注已被其他用户修改（当前版本v${currentVersion}），是否刷新数据后重试？`,
          '冲突提示',
          {
            confirmButtonText: '刷新并重试',
            cancelButtonText: '取消',
            type: 'warning'
          }
        )
        emit('refresh-needed')
      } catch {
        // 用户取消
      }
    } else {
      console.error('Submit annotation error:', error)
    }
  } finally {
    isSubmitting.value = false
  }
}
</script>

<style lang="scss" scoped>
.annotation-editor {
  background: #fff;
  border-radius: 8px;
  overflow: hidden;
}

.editor-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px;
  border-bottom: 1px solid #ebeef5;

  h4 {
    margin: 0;
    font-size: 16px;
    font-weight: 600;
    color: #303133;
  }
}

.editor-body {
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.selected-text-preview {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 12px;
  background: #f5f7fa;
  border-radius: 4px;
  font-size: 13px;
  color: #606266;

  .text {
    font-style: italic;
    overflow: hidden;
    text-overflow: ellipsis;
    display: -webkit-box;
    -webkit-line-clamp: 2;
    -webkit-box-orient: vertical;
  }
}

.type-selector,
.visibility-selector,
.color-selector {
  display: flex;
  align-items: center;
  gap: 12px;

  .label {
    font-size: 13px;
    color: #606266;
    min-width: 70px;
  }
}

.type-selector {
  .type-label {
    margin-left: 4px;
  }
}

.user-selector {
  width: 100%;
}

.color-options {
  display: flex;
  gap: 8px;
}

.color-option {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  cursor: pointer;
  border: 2px solid transparent;
  transition: all 0.2s;

  &.active {
    border-color: #409EFF;
    transform: scale(1.1);
  }

  &:hover {
    transform: scale(1.1);
  }
}

.content-editor {
  :deep(.el-textarea__inner) {
    font-size: 14px;
    line-height: 1.6;
  }
}

.suggestion-input {
  display: flex;
  align-items: center;
  gap: 12px;

  .label {
    font-size: 13px;
    color: #606266;
    min-width: 70px;
  }
}

.editor-footer {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  padding: 16px;
  border-top: 1px solid #ebeef5;
  background: #f5f7fa;
}

@media (max-width: 768px) {
  .editor-body {
    padding: 12px;
    gap: 12px;
  }

  .type-selector,
  .visibility-selector {
    flex-wrap: wrap;

    .label {
      min-width: 100%;
    }
  }

  .editor-header,
  .editor-footer {
    padding: 12px;
  }
}
</style>
