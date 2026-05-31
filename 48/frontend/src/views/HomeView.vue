<template>
  <div class="container">
    <div class="page-header">
      <h1 class="page-title">视频列表</h1>
    </div>

    <div v-if="loading" class="loading-overlay">
      <el-icon class="is-loading" :size="32"><Loading /></el-icon>
    </div>

    <el-row v-if="videos.length > 0" :gutter="20">
      <el-col v-for="video in videos" :key="video.id" :xs="12" :sm="8" :md="6" :lg="4">
        <div class="video-card" @click="playVideo(video)">
          <el-card shadow="hover" :body-style="{ padding: 0 }">
            <div class="video-cover-wrapper">
              <img
                v-if="video.cover_image"
                :src="video.cover_image"
                class="video-cover"
              />
              <div v-else class="video-cover video-cover-placeholder">
                <el-icon :size="48"><VideoCamera /></el-icon>
              </div>
              <el-tag
                v-if="video.status !== 'ready'"
                :type="statusTagType(video.status)"
                size="small"
                class="status-tag"
              >
                {{ statusText(video.status) }}
              </el-tag>
              <div class="play-overlay">
                <el-icon :size="48" color="#fff"><VideoPlay /></el-icon>
              </div>
            </div>
            <div class="video-info">
              <div class="video-title" :title="video.title">{{ video.title }}</div>
              <div class="video-meta">
                <span>{{ video.play_count }} 次播放</span>
                <span v-if="video.duration"> · {{ formatDuration(video.duration) }}</span>
              </div>
            </div>
          </el-card>
        </div>
      </el-col>
    </el-row>

    <el-empty v-else description="暂无视频" />
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useVideoStore } from '@/stores/video'

const router = useRouter()
const store = useVideoStore()
const videos = ref([])
const loading = ref(false)

onMounted(async () => {
  loading.value = true
  try {
    await store.fetchVideos()
    videos.value = store.videos
  } finally {
    loading.value = false
  }
})

function playVideo(video) {
  if (video.status === 'ready') {
    router.push(`/video/${video.id}`)
  }
}

function statusTagType(status) {
  const map = { pending: 'warning', processing: 'primary', ready: 'success', failed: 'danger' }
  return map[status] || 'info'
}

function statusText(status) {
  const map = { pending: '待转码', processing: '转码中', ready: '可播放', failed: '转码失败' }
  return map[status] || status
}

function formatDuration(seconds) {
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const s = Math.floor(seconds % 60)
  if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
  return `${m}:${String(s).padStart(2, '0')}`
}
</script>

<style scoped>
.video-cover-wrapper {
  position: relative;
  width: 100%;
  height: 180px;
  overflow: hidden;
  background: #e4e7ed;
}

.video-cover-placeholder {
  display: flex;
  align-items: center;
  justify-content: center;
  color: #c0c4cc;
}

.play-overlay {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.3);
  display: flex;
  align-items: center;
  justify-content: center;
  opacity: 0;
  transition: opacity 0.2s;
}

.video-card:hover .play-overlay {
  opacity: 1;
}

.status-tag {
  position: absolute;
  top: 8px;
  right: 8px;
}
</style>
