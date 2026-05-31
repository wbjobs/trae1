<template>
  <el-dialog
    :model-value="visible"
    @update:model-value="val => $emit('update:visible', val)"
    title="资源详情"
    width="700px"
    destroy-on-close
  >
    <div v-loading="loading" class="detail-content">
      <template v-if="detail">
        <el-descriptions :column="2" border>
          <el-descriptions-item label="资源名称" :span="2">
            {{ detail.name || '未知资源' }}
          </el-descriptions-item>
          <el-descriptions-item label="InfoHash" :span="2">
            <el-tag type="primary" size="small">{{ detail.infohash }}</el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="总大小">
            <span v-if="detail.total_size">{{ formatSize(detail.total_size) }}</span>
            <span v-else style="color: #c0c4cc;">-</span>
          </el-descriptions-item>
          <el-descriptions-item label="状态">
            <el-tag :type="getStatusType(detail.status)" size="small">
              {{ getStatusLabel(detail.status) }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="做种数">
            <el-tag type="success" size="small">{{ detail.seeders || 0 }}</el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="下载数">
            <el-tag type="warning" size="small">{{ detail.leechers || 0 }}</el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="下载量">
            <el-tag type="danger" size="small">{{ detail.download_count || 0 }}</el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="分片长度">
            <span v-if="detail.piece_length">{{ formatSize(detail.piece_length) }}</span>
            <span v-else style="color: #c0c4cc;">-</span>
          </el-descriptions-item>
          <el-descriptions-item label="分片数量">
            {{ detail.piece_count || 0 }}
          </el-descriptions-item>
          <el-descriptions-item label="首次发现">
            <span v-if="detail.first_seen">{{ formatTime(detail.first_seen) }}</span>
            <span v-else style="color: #c0c4cc;">-</span>
          </el-descriptions-item>
          <el-descriptions-item label="最近发现">
            <span v-if="detail.last_seen">{{ formatTime(detail.last_seen) }}</span>
            <span v-else style="color: #c0c4cc;">-</span>
          </el-descriptions-item>
        </el-descriptions>

        <el-divider>文件列表</el-divider>
        <div v-if="fileList.length > 0" class="file-list">
          <div v-for="(file, idx) in fileList" :key="idx" class="file-item">
            <el-icon color="#409eff"><FolderOpened /></el-icon>
            <span class="file-path">{{ file.path }}</span>
            <el-tag size="small" type="info" class="file-size">{{ file.size }}</el-tag>
          </div>
        </div>
        <el-empty v-else description="暂无文件信息" :image-size="60" />

        <el-divider>磁力链接</el-divider>
        <div class="magnet-link">
          <el-input
            :model-value="magnetLink"
            readonly
            size="small"
          >
            <template #append>
              <el-button @click="copyMagnet">
                <el-icon><CopyDocument /></el-icon>
              </el-button>
            </template>
          </el-input>
        </div>
      </template>
      <el-empty v-else-if="!loading" description="资源不存在" />
    </div>

    <template #footer>
      <el-button @click="$emit('update:visible', false)">关闭</el-button>
    </template>
  </el-dialog>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { fetchResourceDetail } from '../api/index.js'

const props = defineProps({
  visible: { type: Boolean, default: false },
  infohash: { type: String, default: '' }
})

const emit = defineEmits(['update:visible'])

const loading = ref(false)
const detail = ref(null)

const fileList = computed(() => {
  if (!detail.value?.files) return []
  try {
    const files = typeof detail.value.files === 'string'
      ? JSON.parse(detail.value.files)
      : detail.value.files
    return files.map(f => {
      const match = typeof f === 'string' && f.match(/^(.+?)\s*\((\d+)\s*bytes\)$/)
      if (match) {
        return { path: match[1], size: formatSize(parseInt(match[2])) }
      }
      return { path: f, size: '-' }
    })
  } catch {
    return []
  }
})

const magnetLink = computed(() => {
  if (!detail.value?.infohash) return ''
  let link = `magnet:?xt=urn:btih:${detail.value.infohash}`
  if (detail.value?.name) {
    link += `&dn=${encodeURIComponent(detail.value.name)}`
  }
  return link
})

async function loadDetail() {
  if (!props.infohash) return
  loading.value = true
  detail.value = null
  try {
    detail.value = await fetchResourceDetail(props.infohash)
  } catch (e) {
    ElMessage.error('加载详情失败: ' + (e.response?.data?.error || e.message))
  } finally {
    loading.value = false
  }
}

watch(() => [props.visible, props.infohash], ([vis, ih]) => {
  if (vis && ih) loadDetail()
})

function formatSize(bytes) {
  if (!bytes) return '-'
  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB']
  let size = bytes
  let idx = 0
  while (size >= 1024 && idx < units.length - 1) {
    size /= 1024
    idx++
  }
  return size.toFixed(2) + ' ' + units[idx]
}

function formatTime(timestamp) {
  if (!timestamp) return '-'
  const d = new Date(timestamp * 1000)
  return d.toLocaleString('zh-CN')
}

function getStatusType(status) {
  switch (status) {
    case 'parsed': return 'success'
    case 'pending': return 'warning'
    default: return 'info'
  }
}

function getStatusLabel(status) {
  switch (status) {
    case 'parsed': return '已解析'
    case 'pending': return '待解析'
    default: return status || '未知'
  }
}

function copyMagnet() {
  navigator.clipboard.writeText(magnetLink.value).then(() => {
    ElMessage.success('磁力链接已复制到剪贴板')
  })
}
</script>

<style scoped>
.detail-content {
  min-height: 200px;
}

.file-list {
  max-height: 300px;
  overflow-y: auto;
  border: 1px solid #ebeef5;
  border-radius: 4px;
  padding: 10px;
}

.file-item {
  display: flex;
  align-items: center;
  padding: 6px 0;
  border-bottom: 1px solid #f5f5f5;
}

.file-item:last-child {
  border-bottom: none;
}

.file-path {
  margin-left: 8px;
  flex: 1;
  font-size: 13px;
  color: #303133;
}

.file-size {
  flex-shrink: 0;
}

.magnet-link {
  margin-top: 10px;
}
</style>
