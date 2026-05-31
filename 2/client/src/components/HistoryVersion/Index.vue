<template>
  <div class="history-version">
    <div class="version-header">
      <h4>历史版本</h4>
      <div class="version-actions">
        <el-button
          type="primary"
          size="small"
          :loading="isComparing"
          @click="handleCompare"
        >
          <el-icon><Comparison /></el-icon>
          对比版本
        </el-button>
      </div>
    </div>

    <div v-if="versions.length > 0" class="version-timeline">
      <div
        v-for="(version, index) in versions"
        :key="version._id"
        class="version-item"
        :class="{ 
          selected: selectedVersion?._id === version._id,
          comparing: compareVersions.includes(version._id)
        }"
        @click="handleSelectVersion(version)"
      >
        <div class="version-indicator">
          <div class="version-dot" />
          <div v-if="index < versions.length - 1" class="version-line" />
        </div>
        <div class="version-content">
          <div class="version-header">
            <el-tag size="small">v{{ version.version }}</el-tag>
            <span class="version-title">{{ version.title }}</span>
          </div>
          <div class="version-meta">
            <span class="meta-item">
              <el-icon><User /></el-icon>
              {{ getCreatorName(version.createdBy) }}
            </span>
            <span class="meta-item">
              <el-icon><Clock /></el-icon>
              {{ formatTime(version.createdAt) }}
            </span>
          </div>
          <div v-if="version.changeDescription" class="version-description">
            {{ version.changeDescription }}
          </div>
          <div class="version-actions-row">
            <el-checkbox
              v-if="isComparing"
              :model-value="compareVersions.includes(version._id)"
              :disabled="compareVersions.length >= 2 && !compareVersions.includes(version._id)"
              @change="(val) => handleToggleCompare(version._id, val)"
            >
              选择对比
            </el-checkbox>
            <el-button
              v-else
              text
              size="small"
              type="primary"
              @click.stop="handlePreview(version)"
            >
              预览
            </el-button>
            <el-button
              v-else
              text
              size="small"
              type="success"
              :disabled="version.version === currentVersion"
              @click.stop="handleRestore(version)"
            >
              恢复到此版本
            </el-button>
          </div>
        </div>
      </div>
    </div>

    <el-empty
      v-else
      description="暂无历史版本"
      :image-size="100"
    />

    <el-dialog
      v-model="showPreview"
      :title="`预览版本 v${previewVersion?.version}`"
      width="800px"
      destroy-on-close
    >
      <div v-if="previewVersion" class="preview-content">
        <div class="preview-meta">
          <el-tag>{{ previewVersion.title }}</el-tag>
          <span>由 {{ getCreatorName(previewVersion.createdBy) }} 更新于 {{ formatTime(previewVersion.createdAt) }}</span>
        </div>
        <div class="preview-body">
          <div 
            v-if="isMarkdown(previewVersion.content)" 
            class="markdown-preview" 
            v-html="renderMarkdown(previewVersion.content)"
          />
          <pre v-else class="text-preview">{{ previewVersion.content }}</pre>
        </div>
      </div>
      <template #footer>
        <el-button @click="showPreview = false">关闭</el-button>
        <el-button
          type="primary"
          :disabled="previewVersion?.version === currentVersion"
          @click="handleRestore(previewVersion)"
        >
          恢复到此版本
        </el-button>
      </template>
    </el-dialog>

    <el-dialog
      v-model="showCompare"
      title="版本对比"
      width="1000px"
      destroy-on-close
    >
      <div v-if="compareData" class="compare-content">
        <div class="compare-header">
          <div class="compare-version">
            <el-tag type="danger">旧版本 v{{ compareData.oldVersion?.version }}</el-tag>
            <span>{{ getCreatorName(compareData.oldVersion?.createdBy) }} - {{ formatTime(compareData.oldVersion?.createdAt) }}</span>
          </div>
          <el-icon class="compare-arrow"><Right /></el-icon>
          <div class="compare-version">
            <el-tag type="success">新版本 v{{ compareData.newVersion?.version }}</el-tag>
            <span>{{ getCreatorName(compareData.newVersion?.createdBy) }} - {{ formatTime(compareData.newVersion?.createdAt) }}</span>
          </div>
        </div>
        <div class="compare-body">
          <div class="compare-old">
            <h5>旧版本内容</h5>
            <pre>{{ compareData.oldVersion?.content }}</pre>
          </div>
          <div class="compare-new">
            <h5>新版本内容</h5>
            <pre>{{ compareData.newVersion?.content }}</pre>
          </div>
        </div>
      </div>
      <template #footer>
        <el-button @click="showCompare = false">关闭</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { restoreVersion, getVersion } from '@/utils/api'
import { ElMessage, ElMessageBox } from 'element-plus'
import { marked } from 'marked'

const props = defineProps({
  versions: {
    type: Array,
    default: () => []
  },
  currentVersion: {
    type: Number,
    default: 1
  }
})

const emit = defineEmits(['restore', 'refresh'])

const selectedVersion = ref(null)
const previewVersion = ref(null)
const showPreview = ref(false)
const isComparing = ref(false)
const compareVersions = ref([])
const showCompare = ref(false)
const compareData = ref(null)

const formatTime = (time) => {
  return new Date(time).toLocaleString('zh-CN')
}

const getCreatorName = (creator) => {
  if (!creator) return '未知用户'
  return typeof creator === 'string' ? '用户' : creator.username || '未知用户'
}

const isMarkdown = (content) => {
  return content?.includes('#') || content?.includes('```')
}

const renderMarkdown = (content) => {
  return marked(content || '')
}

const handleSelectVersion = (version) => {
  selectedVersion.value = version
}

const handlePreview = async (version) => {
  try {
    const response = await getVersion(version._id)
    previewVersion.value = response.data
    showPreview.value = true
  } catch (error) {
    console.error('Preview version error:', error)
  }
}

const handleRestore = async (version) => {
  if (!version) return

  try {
    await ElMessageBox.confirm(
      `确定要恢复到版本 v${version.version} 吗？当前内容将被保存为新版本。`,
      '版本恢复',
      {
        confirmButtonText: '确定恢复',
        cancelButtonText: '取消',
        type: 'warning'
      }
    )

    const response = await restoreVersion(version._id)
    ElMessage.success('版本恢复成功')
    emit('restore', response.data)
    showPreview.value = false
  } catch (error) {
    if (error !== 'cancel') {
      console.error('Restore version error:', error)
    }
  }
}

const handleCompare = () => {
  if (compareVersions.value.length < 2) {
    isComparing.value = !isComparing.value
    if (!isComparing.value) {
      compareVersions.value = []
    }
  } else {
    showCompareDialog()
  }
}

const handleToggleCompare = (versionId, checked) => {
  if (checked) {
    if (compareVersions.value.length < 2) {
      compareVersions.value.push(versionId)
    }
  } else {
    compareVersions.value = compareVersions.value.filter(id => id !== versionId)
  }
}

const showCompareDialog = async () => {
  if (compareVersions.value.length !== 2) {
    ElMessage.warning('请选择两个版本进行对比')
    return
  }

  try {
    const [oldResponse, newResponse] = await Promise.all([
      getVersion(compareVersions.value[0]),
      getVersion(compareVersions.value[1])
    ])

    compareData.value = {
      oldVersion: oldResponse.data,
      newVersion: newResponse.data
    }

    showCompare.value = true
  } catch (error) {
    console.error('Compare versions error:', error)
  }
}

watch(() => props.versions, () => {
  selectedVersion.value = null
  isComparing.value = false
  compareVersions.value = []
})
</script>

<style lang="scss" scoped>
.history-version {
  background: #fff;
  border-radius: 8px;
  padding: 24px;
}

.version-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 24px;

  h4 {
    margin: 0;
    font-size: 18px;
    font-weight: 600;
    color: #303133;
  }
}

.version-timeline {
  position: relative;
}

.version-item {
  display: flex;
  gap: 16px;
  padding: 16px;
  border-radius: 8px;
  cursor: pointer;
  transition: background 0.2s;

  &:hover {
    background: #f5f7fa;
  }

  &.selected {
    background: #ecf5ff;
    border: 1px solid #d9ecff;
  }

  &.comparing {
    background: #fdf6ec;
    border: 1px solid #faecd8;
  }
}

.version-indicator {
  display: flex;
  flex-direction: column;
  align-items: center;

  .version-dot {
    width: 12px;
    height: 12px;
    border-radius: 50%;
    background: #409EFF;
    flex-shrink: 0;
  }

  .version-line {
    width: 2px;
    flex: 1;
    background: #dcdfe6;
    margin-top: 8px;
  }
}

.version-content {
  flex: 1;
  min-width: 0;
}

.version-header {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;

  .version-title {
    font-weight: 500;
    color: #303133;
  }
}

.version-meta {
  display: flex;
  gap: 16px;
  font-size: 12px;
  color: #909399;
  margin-bottom: 8px;

  .meta-item {
    display: flex;
    align-items: center;
    gap: 4px;
  }
}

.version-description {
  font-size: 13px;
  color: #606266;
  margin-bottom: 8px;
  padding: 8px;
  background: #f5f7fa;
  border-radius: 4px;
}

.version-actions-row {
  display: flex;
  gap: 8px;
}

.preview-content {
  .preview-meta {
    display: flex;
    align-items: center;
    gap: 12px;
    margin-bottom: 16px;
    font-size: 13px;
    color: #606266;
  }

  .preview-body {
    max-height: 500px;
    overflow-y: auto;
    padding: 16px;
    background: #f5f7fa;
    border-radius: 4px;

    .markdown-preview {
      line-height: 1.8;
    }

    .text-preview {
      white-space: pre-wrap;
      word-break: break-word;
      font-family: 'Monaco', 'Menlo', monospace;
    }
  }
}

.compare-content {
  .compare-header {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 16px;
    margin-bottom: 16px;

    .compare-version {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 4px;
      font-size: 12px;
      color: #606266;
    }

    .compare-arrow {
      font-size: 24px;
      color: #409EFF;
    }
  }

  .compare-body {
    display: flex;
    gap: 16px;

    .compare-old,
    .compare-new {
      flex: 1;
      padding: 16px;
      border-radius: 8px;

      h5 {
        margin: 0 0 12px 0;
        font-size: 14px;
      }

      pre {
        max-height: 400px;
        overflow-y: auto;
        padding: 12px;
        background: #fff;
        border-radius: 4px;
        white-space: pre-wrap;
        word-break: break-word;
        font-size: 13px;
        line-height: 1.6;
      }
    }

    .compare-old {
      background: #fef0f0;

      h5 {
        color: #F56C6C;
      }
    }

    .compare-new {
      background: #f0f9eb;

      h5 {
        color: #67C23A;
      }
    }
  }
}

@media (max-width: 768px) {
  .history-version {
    padding: 16px;
  }

  .version-header {
    flex-direction: column;
    align-items: flex-start;
    gap: 12px;
  }

  .compare-content {
    .compare-body {
      flex-direction: column;
    }
  }
}
</style>
