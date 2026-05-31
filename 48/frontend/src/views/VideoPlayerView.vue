<template>
  <div class="container">
    <div v-if="!video" class="loading-overlay">
      <el-icon class="is-loading" :size="32"><Loading /></el-icon>
    </div>

    <template v-else>
      <el-row :gutter="20">
        <el-col :span="16">
          <div class="video-player-wrapper">
            <video
              ref="videoRef"
              controls
              crossorigin="anonymous"
              @play="onPlay"
              @pause="onPause"
              @ended="onEnded"
              @timeupdate="onTimeUpdate"
              @error="onVideoError"
            ></video>
          </div>

          <el-card v-if="video" class="video-detail" shadow="never">
            <h2>{{ video.title }}</h2>
            <div class="video-meta-row">
              <el-tag type="info" size="small">
                时长: {{ formatDuration(video.duration) }}
              </el-tag>
              <el-tag type="info" size="small">
                播放: {{ video.play_count }} 次
              </el-tag>
              <el-tag
                v-for="res in video.resolutions"
                :key="res.name"
                type="success"
                size="small"
              >
                {{ res.name }} ({{ res.width }}x{{ res.height }})
              </el-tag>
            </div>
            <p v-if="video.description" class="video-desc">
              {{ video.description }}
            </p>
          </el-card>
        </el-col>

        <el-col :span="8">
          <el-card shadow="never">
            <template #header>
              <span>播放信息</span>
            </template>

            <div class="info-item">
              <span class="label">密钥状态:</span>
              <el-tag :type="keyStatusTagType">
                {{ keyStatusText }}
              </el-tag>
            </div>

            <div class="info-item">
              <span class="label">当前密钥索引:</span>
              <span class="value">{{ currentKeyIndex ?? 'N/A' }}</span>
            </div>

            <div class="info-item">
              <span class="label">有效密钥范围:</span>
              <span class="value">{{ minKeyIndex }} - {{ maxKeyIndex }}</span>
            </div>

            <div class="info-item">
              <span class="label">观看进度:</span>
              <el-progress
                :percentage="progressPercent"
                :stroke-width="8"
              />
            </div>

            <div class="info-item">
              <span class="label">观看时长:</span>
              <span class="value">{{ formatDuration(watchedDuration) }}</span>
            </div>

            <div class="info-item">
              <span class="label">令牌剩余:</span>
              <span class="value">{{ tokenRemaining }}s</span>
            </div>

            <el-divider />

            <div class="info-item">
              <span class="label">重试次数:</span>
              <span class="value">{{ retryCount }}</span>
            </div>

            <div class="info-item">
              <span class="label">上次错误:</span>
              <span class="value error" v-if="lastError">{{ lastError }}</span>
              <span class="value" v-else>无</span>
            </div>
          </el-card>
        </el-col>
      </el-row>
    </template>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, computed, watch } from 'vue'
import { useRoute } from 'vue-router'
import { ElMessage, ElNotification } from 'element-plus'
import Hls from 'hls.js'
import { videoApi } from '@/api'
import { useVideoStore } from '@/stores/video'

const route = useRoute()
const store = useVideoStore()
const videoRef = ref(null)
const video = ref(null)
const token = ref('')
const tokenTimer = ref(null)
const tokenRemaining = ref(300)

const watchedDuration = ref(0)
const progressPercent = ref(0)
const currentKeyIndex = ref(null)
const minKeyIndex = ref(0)
const maxKeyIndex = ref(0)
const retryCount = ref(0)
const lastError = ref('')
const keyStatusText = ref('等待播放')
const keyStatusTagType = ref('info')

let hls = null
let playbackRecordId = null
let hlsEventHandlers = []

const keyStatusTagTypeComputed = computed(() => keyStatusTagType.value)

onMounted(async () => {
  try {
    const res = await videoApi.get(route.params.id)
    video.value = res.data

    if (video.value.status !== 'ready') {
      ElMessage.warning('视频尚未就绪，无法播放')
      return
    }

    await initPlayback()
  } catch (e) {
    console.error('Failed to load video:', e)
    ElMessage.error('加载视频失败')
  }
})

onUnmounted(() => {
  cleanup()
})

async function initPlayback() {
  try {
    const tokenRes = await videoApi.getToken(route.params.id)
    token.value = tokenRes.data.token
    tokenRemaining.value = tokenRes.data.expires_in

    startTokenTimer()

    const masterUrl = `/api/videos/${route.params.id}/master.m3u8?token=${token.value}`

    if (Hls.isSupported()) {
      initHls(masterUrl)
    } else if (videoRef.value.canPlayType('application/vnd.apple.mpegurl')) {
      videoRef.value.src = masterUrl
    } else {
      ElMessage.error('您的浏览器不支持HLS播放')
    }
  } catch (e) {
    console.error('Init playback failed:', e)
    ElMessage.error('初始化播放失败')
  }
}

function initHls(masterUrl) {
  hls = new Hls({
    enableWorker: true,
    lowLatencyMode: false,
    backBufferLength: 60,
    maxBufferLength: 30,
    maxMaxBufferLength: 60,
    liveDurationInfinity: true,
    manifestLoadingMaxRetry: 5,
    levelLoadingMaxRetry: 5,
    fragLoadingMaxRetry: 5,
    keyLoadingMaxRetry: 5,
    manifestLoadingRetryDelay: 1000,
    keyLoadingRetryDelay: 2000,
    fragLoadingRetryDelay: 1000,
  })

  hls.loadSource(masterUrl)
  hls.attachMedia(videoRef.value)

  const handleManifestLoaded = (event, data) => {
    console.log('Manifest loaded:', data)
    parseManifestKeyInfo(data)
  }

  const handleLevelLoaded = (event, data) => {
    console.log('Level loaded:', data)
    if (data.details && data.details.live) {
      parseLevelKeyInfo(data)
    }
  }

  const handleKeyLoaded = (event, data) => {
    console.log('Key loaded:', data)
    if (data.frag && data.frag.key) {
      const keyIndexMatch = data.frag.relurl?.match(/index=(\d+)/)
      if (keyIndexMatch) {
        currentKeyIndex.value = parseInt(keyIndexMatch[1])
      }
    }
    keyStatusText.value = '密钥已获取'
    keyStatusTagType.value = 'success'
    lastError.value = ''
  }

  const handleKeyLoading = (event, data) => {
    console.log('Key loading:', data)
    if (data.frag && data.frag.decryptdata) {
      const origUri = data.frag.decryptdata.uri
      if (origUri && !origUri.includes('index=')) {
        const currentIdx = currentKeyIndex.value ?? minKeyIndex.value
        data.frag.decryptdata.uri = appendUrlParam(origUri, 'index', currentIdx)
      }
    }
  }

  const handleError = (event, data) => {
    console.error('HLS Error:', data)
    handleHlsError(data)
  }

  const handleFragLoaded = (event, data) => {
    if (data.frag) {
      const level = hls.levels[data.frag.level]
      if (level && level.bitrate) {
        store.currentBitrate = Math.round(level.bitrate / 1000)
      }
      if (level && level.height) {
        store.currentResolution = `${level.height}p`
      }
    }
  }

  const handleFragChanged = (event, data) => {
    if (data.frag && data.frag.decryptdata && data.frag.decryptdata.uri) {
      const keyIdxMatch = data.frag.decryptdata.uri.match(/index=(\d+)/)
      if (keyIdxMatch) {
        const newIndex = parseInt(keyIdxMatch[1])
        if (newIndex !== currentKeyIndex.value) {
          currentKeyIndex.value = newIndex
          console.log(`Key index switched to: ${newIndex}`)
        }
      }
    }
  }

  hls.on(Hls.Events.MANIFEST_LOADED, handleManifestLoaded)
  hls.on(Hls.Events.LEVEL_LOADED, handleLevelLoaded)
  hls.on(Hls.Events.KEY_LOADING, handleKeyLoading)
  hls.on(Hls.Events.KEY_LOADED, handleKeyLoaded)
  hls.on(Hls.Events.FRAG_LOADED, handleFragLoaded)
  hls.on(Hls.Events.FRAG_CHANGED, handleFragChanged)
  hls.on(Hls.Events.ERROR, handleError)

  hlsEventHandlers = [
    [Hls.Events.MANIFEST_LOADED, handleManifestLoaded],
    [Hls.Events.LEVEL_LOADED, handleLevelLoaded],
    [Hls.Events.KEY_LOADING, handleKeyLoading],
    [Hls.Events.KEY_LOADED, handleKeyLoaded],
    [Hls.Events.FRAG_LOADED, handleFragLoaded],
    [Hls.Events.FRAG_CHANGED, handleFragChanged],
    [Hls.Events.ERROR, handleError],
  ]
}

function parseManifestKeyInfo(data) {
  if (data.headers) {
    const currentIdx = data.headers['x-key-current-index']
    const minIdx = data.headers['x-key-min-index']
    const maxIdx = data.headers['x-key-max-index']
    if (currentIdx) currentKeyIndex.value = parseInt(currentIdx)
    if (minIdx) minKeyIndex.value = parseInt(minIdx)
    if (maxIdx) maxKeyIndex.value = parseInt(maxIdx)
  }
}

function parseLevelKeyInfo(data) {
  if (data.headers) {
    const currentIdx = data.headers['x-key-current-index']
    const minIdx = data.headers['x-key-min-index']
    const maxIdx = data.headers['x-key-max-index']
    if (currentIdx) currentKeyIndex.value = parseInt(currentIdx)
    if (minIdx) minKeyIndex.value = parseInt(minIdx)
    if (maxIdx) maxKeyIndex.value = parseInt(maxIdx)
  }
}

function appendUrlParam(url, key, value) {
  const separator = url.includes('?') ? '&' : '?'
  return `${url}${separator}${key}=${value}`
}

function handleHlsError(data) {
  if (data.fatal) {
    if (data.type === Hls.ErrorTypes.NETWORK_ERROR) {
      if (data.details === Hls.ErrorDetails.KEY_LOAD_ERROR) {
        keyStatusText.value = '密钥加载失败，正在重试'
        keyStatusTagType.value = 'warning'
        lastError.value = 'KEY_LOAD_ERROR'
        retryCount.value++
        const currentIdx = currentKeyIndex.value
        if (currentIdx !== null && currentIdx > minKeyIndex.value) {
          const newIdx = currentIdx - 1
          console.log(`Retrying key with older index: ${newIdx}`)
          currentKeyIndex.value = newIdx
          ElNotification({
            title: '密钥轮换',
            message: `正在使用上一个密钥版本（索引: ${newIdx}）继续播放`,
            type: 'warning',
            duration: 3000,
          })
        }
        try {
          hls.startLoad()
        } catch (e) {
          console.error('Failed to recover from key error:', e)
        }
      } else {
        keyStatusText.value = '网络错误'
        keyStatusTagType.value = 'danger'
        lastError.value = data.details || 'NETWORK_ERROR'
      }
    } else if (data.type === Hls.ErrorTypes.MEDIA_ERROR) {
      keyStatusText.value = '媒体错误'
      keyStatusTagType.value = 'danger'
      lastError.value = data.details || 'MEDIA_ERROR'
      try {
        hls.recoverMediaError()
      } catch (e) {
        console.error('Media recovery failed:', e)
      }
    } else if (data.type === Hls.ErrorTypes.DECRYPT_ERROR) {
      keyStatusText.value = '解密错误，尝试旧密钥'
      keyStatusTagType.value = 'warning'
      lastError.value = 'DECRYPT_ERROR'
      retryCount.value++
      const currentIdx = currentKeyIndex.value
      if (currentIdx !== null && currentIdx > minKeyIndex.value) {
        currentKeyIndex.value = currentIdx - 1
        console.log(`Decrypt error, switching to key index: ${currentKeyIndex.value}`)
        ElNotification({
          title: '解密失败',
          message: `正在切换到上一个密钥版本（索引: ${currentKeyIndex.value}）重试`,
          type: 'warning',
          duration: 3000,
        })
      }
    }
  } else {
    if (data.type === Hls.ErrorTypes.NETWORK_ERROR &&
        data.details === Hls.ErrorDetails.KEY_LOAD_ERROR) {
      keyStatusText.value = '密钥加载警告'
      keyStatusTagType.value = 'warning'
      console.warn('Non-fatal key load error:', data)
    }
  }
}

async function onPlay() {
  try {
    const res = await videoApi.playbackStart(route.params.id, token.value)
    playbackRecordId = res.data.record_id
    store.startPlaybackTracking(playbackRecordId, route.params.id)
    keyStatusText.value = '播放中'
    keyStatusTagType.value = 'success'
  } catch (e) {
    console.error('Start playback failed:', e)
    if (e.response?.status === 403) {
      ElMessage.warning('播放令牌已过期，正在刷新...')
      try {
        const tokenRes = await videoApi.getToken(route.params.id)
        token.value = tokenRes.data.token
        tokenRemaining.value = tokenRes.data.expires_in
        const newMasterUrl = `/api/videos/${route.params.id}/master.m3u8?token=${token.value}`
        if (hls) {
          hls.loadSource(newMasterUrl)
        } else {
          videoRef.value.src = newMasterUrl
        }
      } catch (refreshErr) {
        ElMessage.error('刷新令牌失败')
      }
    }
  }
}

function onPause() {
  if (playbackRecordId) {
    store.sendPlaybackUpdate()
  }
}

async function onEnded() {
  if (playbackRecordId) {
    await store.endPlayback(true)
    playbackRecordId = null
  }
  keyStatusText.value = '播放完成'
  keyStatusTagType.value = 'success'
}

function onTimeUpdate() {
  if (videoRef.value && video.value) {
    const current = videoRef.value.currentTime
    const duration = video.value.duration
    if (duration > 0) {
      progressPercent.value = Math.round((current / duration) * 100)
      watchedDuration.value = current
    }
  }
}

function onVideoError(e) {
  console.error('Video error:', e)
  lastError.value = 'Video element error'
  keyStatusText.value = '视频错误'
  keyStatusTagType.value = 'danger'
}

function startTokenTimer() {
  if (tokenTimer.value) clearInterval(tokenTimer.value)
  tokenTimer.value = setInterval(() => {
    tokenRemaining.value--
    if (tokenRemaining.value <= 60) {
      refreshToken()
    }
    if (tokenRemaining.value <= 0) {
      tokenRemaining.value = 0
    }
  }, 1000)
}

async function refreshToken() {
  try {
    const res = await videoApi.getToken(route.params.id)
    token.value = res.data.token
    tokenRemaining.value = res.data.expires_in
    if (hls) {
      const newMasterUrl = `/api/videos/${route.params.id}/master.m3u8?token=${token.value}`
      hls.loadSource(newMasterUrl)
    }
    console.log('Token refreshed')
  } catch (e) {
    console.error('Token refresh failed:', e)
  }
}

function formatDuration(seconds) {
  if (!seconds || seconds <= 0) return '0:00'
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const s = Math.floor(seconds % 60)
  if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
  return `${m}:${String(s).padStart(2, '0')}`
}

function cleanup() {
  if (tokenTimer.value) {
    clearInterval(tokenTimer.value)
    tokenTimer.value = null
  }
  if (playbackRecordId) {
    store.endPlayback(false)
    playbackRecordId = null
  }
  if (hls) {
    for (const [event, handler] of hlsEventHandlers) {
      try {
        hls.off(event, handler)
      } catch (e) {}
    }
    hls.destroy()
    hls = null
  }
}
</script>

<style scoped>
.video-detail {
  margin-top: 20px;
}

.video-detail h2 {
  margin-bottom: 12px;
  font-size: 22px;
}

.video-meta-row {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
  margin-bottom: 12px;
}

.video-desc {
  color: #606266;
  line-height: 1.6;
}

.info-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.info-item .label {
  color: #909399;
  font-size: 14px;
}

.info-item .value {
  color: #303133;
  font-size: 14px;
  font-weight: 500;
}

.info-item .value.error {
  color: #f56c6c;
}
</style>
