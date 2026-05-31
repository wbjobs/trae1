<template>
  <div>
    <div class="page-header">
      <h1 class="page-title">播放统计</h1>
      <el-select
        v-model="selectedVideoId"
        placeholder="选择视频"
        clearable
        style="width: 300px"
        @change="loadStats"
      >
        <el-option
          v-for="video in videos"
          :key="video.id"
          :label="video.title"
          :value="video.id"
        />
      </el-select>
    </div>

    <el-row :gutter="20" v-if="stats">
      <el-col :span="6">
        <div class="stat-card">
          <div class="stat-label">总播放次数</div>
          <div class="stat-value">{{ stats.total_plays }}</div>
          <div class="stat-trend up">24小时内: {{ recentPlays }}</div>
        </div>
      </el-col>
      <el-col :span="6">
        <div class="stat-card">
          <div class="stat-label">完播次数</div>
          <div class="stat-value">{{ stats.completed_plays }}</div>
          <div class="stat-trend">完播率: {{ stats.completion_rate }}%</div>
        </div>
      </el-col>
      <el-col :span="6">
        <div class="stat-card">
          <div class="stat-label">平均观看时长</div>
          <div class="stat-value">{{ formatDuration(stats.avg_watched_duration) }}</div>
          <div class="stat-trend">总时长: {{ formatDuration(stats.total_duration) }}</div>
        </div>
      </el-col>
      <el-col :span="6">
        <div class="stat-card">
          <div class="stat-label">平均码率</div>
          <div class="stat-value">{{ stats.avg_bitrate }} kbps</div>
          <div class="stat-trend">网络质量</div>
        </div>
      </el-col>
    </el-row>

    <el-row :gutter="20" style="margin-top: 20px">
      <el-col :span="12">
        <el-card shadow="never">
          <template #header>
            <span>各视频播放统计</span>
          </template>
          <el-table :data="statsList" v-loading="loadingList" max-height="400" size="small">
            <el-table-column prop="title" label="视频" min-width="150" />
            <el-table-column prop="total_plays" label="播放" width="80" align="center" />
            <el-table-column prop="completed_plays" label="完播" width="80" align="center" />
            <el-table-column prop="completion_rate" label="完播率" width="90" align="center">
              <template #default="{ row }">
                {{ row.completion_rate }}%
              </template>
            </el-table-column>
            <el-table-column prop="avg_bitrate" label="平均码率" width="100" align="center">
              <template #default="{ row }">
                {{ row.avg_bitrate }} kbps
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-col>
      <el-col :span="12">
        <el-card shadow="never">
          <template #header>
            <span>完播率趋势</span>
          </template>
          <div ref="chartRef" style="height: 300px"></div>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { ref, onMounted, nextTick } from 'vue'
import { useRoute } from 'vue-router'
import * as echarts from 'echarts'
import { videoApi } from '@/api'

const route = useRoute()
const videos = ref([])
const stats = ref(null)
const statsList = ref([])
const loadingList = ref(false)
const recentPlays = ref(0)
const selectedVideoId = ref(route.query.video_id || '')
const chartRef = ref(null)
let chartInstance = null

onMounted(async () => {
  try {
    const res = await videoApi.list({ page_size: 100, status: 'ready' })
    videos.value = res.data.results || res.data
  } catch (e) {
    console.error('Load videos failed:', e)
  }
  await loadStats()
  await loadStatsList()
})

async function loadStats() {
  try {
    const videoId = selectedVideoId.value || null
    const res = await videoApi.getStats(videoId)
    stats.value = res.data
  } catch (e) {
    console.error('Load stats failed:', e)
  }
}

async function loadStatsList() {
  loadingList.value = true
  try {
    const res = await videoApi.getStatsList()
    statsList.value = res.data
    await nextTick()
    renderChart()
  } catch (e) {
    console.error('Load stats list failed:', e)
  } finally {
    loadingList.value = false
  }
}

function renderChart() {
  if (!chartRef.value) return

  if (!chartInstance) {
    chartInstance = echarts.init(chartRef.value)
  }

  const data = statsList.value.slice(0, 10)
  const option = {
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'shadow' },
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      containLabel: true,
    },
    xAxis: {
      type: 'category',
      data: data.map((d) => d.title.length > 10 ? d.title.slice(0, 10) + '...' : d.title),
      axisLabel: {
        interval: 0,
        rotate: 30,
      },
    },
    yAxis: {
      type: 'value',
      name: '完播率(%)',
      max: 100,
    },
    series: [
      {
        name: '完播率',
        type: 'bar',
        data: data.map((d) => d.completion_rate),
        itemStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: '#83bff6' },
            { offset: 0.5, color: '#188df0' },
            { offset: 1, color: '#188df0' },
          ]),
        },
        label: {
          show: true,
          position: 'top',
          formatter: '{c}%',
        },
      },
    ],
  }

  chartInstance.setOption(option)
}

function formatDuration(seconds) {
  if (!seconds || seconds <= 0) return '0:00'
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const s = Math.floor(seconds % 60)
  if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
  return `${m}:${String(s).padStart(2, '0')}`
}
</script>

<style scoped>
</style>
