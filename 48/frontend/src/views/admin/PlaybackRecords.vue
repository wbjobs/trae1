<template>
  <div>
    <div class="page-header">
      <h1 class="page-title">播放记录</h1>
    </div>

    <el-card shadow="never">
      <el-table :data="records" v-loading="loading" stripe>
        <el-table-column prop="video_title" label="视频" min-width="200" />
        <el-table-column prop="viewer_ip" label="观众IP" width="140" />
        <el-table-column prop="resolution" label="分辨率" width="100" />
        <el-table-column label="观看时长" width="120">
          <template #default="{ row }">
            {{ formatDuration(row.watched_duration) }}
          </template>
        </el-table-column>
        <el-table-column prop="is_completed" label="完播" width="80" align="center">
          <template #default="{ row }">
            <el-tag v-if="row.is_completed" type="success" size="small">是</el-tag>
            <el-tag v-else type="info" size="small">否</el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="avg_bitrate" label="平均码率" width="120">
          <template #default="{ row }">
            {{ row.avg_bitrate }} kbps
          </template>
        </el-table-column>
        <el-table-column prop="created_at" label="播放时间" width="160">
          <template #default="{ row }">
            {{ formatDate(row.created_at) }}
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { videoApi } from '@/api'

const records = ref([])
const loading = ref(false)

onMounted(async () => {
  loading.value = true
  try {
    const res = await videoApi.listRecords()
    records.value = res.data || res
  } catch (e) {
    console.error('Load records failed:', e)
  } finally {
    loading.value = false
  }
})

function formatDuration(seconds) {
  if (!seconds || seconds <= 0) return '0:00'
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
</script>

<style scoped>
</style>
