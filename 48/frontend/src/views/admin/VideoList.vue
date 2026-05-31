<template>
  <div>
    <div class="page-header">
      <h1 class="page-title">视频管理</h1>
      <el-button type="primary" @click="$router.push('/admin/upload')">
        <el-icon><Upload /></el-icon>
        上传视频
      </el-button>
    </div>

    <el-card shadow="never">
      <el-table :data="videos" v-loading="loading" stripe>
        <el-table-column prop="title" label="视频标题" min-width="200">
          <template #default="{ row }">
            <div class="video-title-cell">
              <img
                v-if="row.cover_image"
                :src="row.cover_image"
                class="video-thumb"
              />
              <span>{{ row.title }}</span>
            </div>
          </template>
        </el-table-column>
        <el-table-column prop="uploader_name" label="上传者" width="120" />
        <el-table-column prop="status" label="状态" width="100">
          <template #default="{ row }">
            <el-tag :type="statusType(row.status)" size="small">
              {{ statusText(row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="duration" label="时长" width="100">
          <template #default="{ row }">
            {{ formatDuration(row.duration) }}
          </template>
        </el-table-column>
        <el-table-column prop="play_count" label="播放次数" width="100" />
        <el-table-column prop="created_at" label="创建时间" width="160">
          <template #default="{ row }">
            {{ formatDate(row.created_at) }}
          </template>
        </el-table-column>
        <el-table-column label="操作" width="200" fixed="right">
          <template #default="{ row }">
            <el-button
              v-if="row.status === 'failed'"
              size="small"
              type="warning"
              @click="handleRetry(row)"
            >
              重试转码
            </el-button>
            <el-button
              v-if="row.status === 'ready'"
              size="small"
              type="primary"
              @click="$router.push(`/video/${row.id}`)"
            >
              播放
            </el-button>
            <el-button
              v-if="row.status === 'ready'"
              size="small"
              @click="viewStats(row)"
            >
              统计
            </el-button>
            <el-button
              size="small"
              type="danger"
              @click="handleDelete(row)"
            >
              删除
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { videoApi } from '@/api'
import { useVideoStore } from '@/stores/video'

const router = useRouter()
const store = useVideoStore()
const videos = ref([])
const loading = ref(false)

onMounted(async () => {
  loading.value = true
  try {
    const res = await videoApi.list({ page_size: 100 })
    videos.value = res.data.results || res.data
  } finally {
    loading.value = false
  }
})

function statusType(status) {
  const map = { pending: 'warning', processing: 'primary', ready: 'success', failed: 'danger' }
  return map[status] || 'info'
}

function statusText(status) {
  const map = { pending: '待转码', processing: '转码中', ready: '已就绪', failed: '转码失败' }
  return map[status] || status
}

function formatDuration(seconds) {
  if (!seconds) return '-'
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const s = Math.floor(seconds % 60)
  if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
  return `${m}:${String(s).padStart(2, '0')}`
}

function formatDate(dateStr) {
  if (!dateStr) return '-'
  return new Date(dateStr).toLocaleString('zh-CN')
}

async function handleRetry(row) {
  try {
    await videoApi.retryTranscode(row.id)
    ElMessage.success('转码任务已重新提交')
    row.status = 'pending'
  } catch (e) {
    ElMessage.error('重试失败')
  }
}

async function handleDelete(row) {
  try {
    await ElMessageBox.confirm(
      `确定要删除视频"${row.title}"吗？此操作不可恢复。`,
      '删除确认',
      { type: 'warning' }
    )
    await videoApi.delete(row.id)
    ElMessage.success('删除成功')
    videos.value = videos.value.filter((v) => v.id !== row.id)
  } catch (e) {
    if (e !== 'cancel') {
      ElMessage.error('删除失败')
    }
  }
}

function viewStats(row) {
  router.push(`/admin/stats?video_id=${row.id}`)
}
</script>

<style scoped>
.video-title-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}

.video-thumb {
  width: 48px;
  height: 32px;
  object-fit: cover;
  border-radius: 4px;
  background: #e4e7ed;
}
</style>
