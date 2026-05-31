<template>
  <div class="task-detail">
    <div class="page-header">
      <div class="header-left">
        <el-button :icon="ArrowLeft" text @click="$router.back()">返回</el-button>
        <h2 class="page-title">{{ task?.name || '任务详情' }}</h2>
        <el-tag :type="getPriorityType(task?.priority)" size="large" v-if="task">
          {{ getPriorityText(task.priority) }}
        </el-tag>
        <el-tag :type="getStatusType(task?.status)" size="large" v-if="task">
          {{ getStatusText(task.status) }}
        </el-tag>
      </div>
      <div class="header-right">
        <el-button
          v-if="task && (task.status === 'PENDING' || task.status === 'FAILED' || task.status === 'STOPPED')"
          type="success"
          :icon="VideoPlay"
          @click="handleStart"
        >启动</el-button>
        <el-button
          v-if="task?.status === 'RUNNING'"
          type="warning"
          :icon="VideoPause"
          @click="handleStop"
        >停止</el-button>
        <el-button
          v-if="task?.status === 'COMPLETED' || task?.status === 'FAILED' || task?.status === 'STOPPED'"
          type="primary"
          :icon="Download"
          @click="showReportDialog = true"
        >导出报告</el-button>
      </div>
    </div>

    <div v-loading="loading" class="detail-content">
      <template v-if="task">
        <el-row :gutter="20" class="metrics-row">
          <el-col :span="6">
            <div class="metric-card metric-total">
              <div class="metric-label">总请求数</div>
              <div class="metric-value">{{ formatNumber(task.totalRequests) }}</div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="metric-card metric-success">
              <div class="metric-label">成功请求</div>
              <div class="metric-value">{{ formatNumber(task.successCount) }}</div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="metric-card metric-failure">
              <div class="metric-label">失败请求</div>
              <div class="metric-value">{{ formatNumber(task.failureCount) }}</div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="metric-card metric-error">
              <div class="metric-label">错误率</div>
              <div class="metric-value">{{ formatPercent(task.errorRate) }}</div>
            </div>
          </el-col>
        </el-row>

        <el-row :gutter="20" class="metrics-row">
          <el-col :span="6">
            <div class="metric-card metric-avg">
              <div class="metric-label">平均响应(ms)</div>
              <div class="metric-value">{{ formatDecimal(task.avgResponseTime) }}</div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="metric-card metric-p95">
              <div class="metric-label">P95响应(ms)</div>
              <div class="metric-value">{{ formatDecimal(task.p95ResponseTime) }}</div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="metric-card metric-p99">
              <div class="metric-label">P99响应(ms)</div>
              <div class="metric-value">{{ formatDecimal(task.p99ResponseTime) }}</div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="metric-card metric-throughput">
              <div class="metric-label">吞吐量(req/s)</div>
              <div class="metric-value">{{ formatDecimal(task.throughput) }}</div>
            </div>
          </el-col>
        </el-row>

        <el-row :gutter="20" class="chart-row">
          <el-col :span="12">
            <div class="card-container">
              <h3>响应时间趋势</h3>
              <div ref="responseTimeChartRef" class="chart-container"></div>
            </div>
          </el-col>
          <el-col :span="12">
            <div class="card-container">
              <h3>响应时间分布</h3>
              <div ref="distributionChartRef" class="chart-container"></div>
            </div>
          </el-col>
        </el-row>

        <el-row :gutter="20" class="chart-row">
          <el-col :span="12">
            <div class="card-container">
              <h3>吞吐量趋势</h3>
              <div ref="throughputChartRef" class="chart-container"></div>
            </div>
          </el-col>
          <el-col :span="12">
            <div class="card-container">
              <h3>成功/失败比例</h3>
              <div ref="statusChartRef" class="chart-container"></div>
            </div>
          </el-col>
        </el-row>

        <div class="card-container mt-20">
          <h3>任务信息</h3>
          <el-descriptions :column="2" border>
            <el-descriptions-item label="任务ID">{{ task.id }}</el-descriptions-item>
            <el-descriptions-item label="任务状态">{{ getStatusText(task.status) }}</el-descriptions-item>
            <el-descriptions-item label="开始时间">{{ formatTime(task.startedAt) }}</el-descriptions-item>
            <el-descriptions-item label="结束时间">{{ formatTime(task.completedAt) }}</el-descriptions-item>
            <el-descriptions-item label="创建时间">{{ formatTime(task.createdAt) }}</el-descriptions-item>
            <el-descriptions-item label="配置ID">{{ task.configId }}</el-descriptions-item>
            <el-descriptions-item label="最小响应(ms)">{{ formatDecimal(task.minResponseTime) }}</el-descriptions-item>
            <el-descriptions-item label="最大响应(ms)">{{ formatDecimal(task.maxResponseTime) }}</el-descriptions-item>
          </el-descriptions>
        </div>
      </template>
    </div>

    <el-dialog v-model="showReportDialog" title="导出报告" width="400px">
      <el-form label-width="100px">
        <el-form-item label="报告格式">
          <el-radio-group v-model="reportFormat">
            <el-radio value="html">HTML</el-radio>
            <el-radio value="excel">Excel</el-radio>
          </el-radio-group>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showReportDialog = false">取消</el-button>
        <el-button type="primary" :loading="reporting" @click="handleExportReport">
          导出
        </el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, watch, nextTick } from 'vue'
import { useRoute } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { ArrowLeft, VideoPlay, VideoPause, Download } from '@element-plus/icons-vue'
import {
  getTask,
  startTask,
  stopTask,
  getTaskTimeline,
  getResponseTimeDistribution,
  downloadReport
} from '@/api/task'
import * as echarts from 'echarts'
import dayjs from 'dayjs'
import { Client } from '@stomp/stompjs'
import SockJS from 'sockjs-client'

const route = useRoute()

const loading = ref(false)
const task = ref(null)
const showReportDialog = ref(false)
const reportFormat = ref('html')
const reporting = ref(false)

const responseTimeChartRef = ref(null)
const distributionChartRef = ref(null)
const throughputChartRef = ref(null)
const statusChartRef = ref(null)

let charts = {}
let refreshInterval = null
let stompClient = null
let wsConnected = false

const connectWebSocket = () => {
  if (stompClient && wsConnected) return

  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
  const wsUrl = `${protocol}//${window.location.host}/ws`
  
  try {
    stompClient = new Client({
      webSocketFactory: () => new SockJS(wsUrl),
      reconnectDelay: 5000,
      heartbeatIncoming: 10000,
      heartbeatOutgoing: 10000,
      onConnect: () => {
        wsConnected = true
        console.log('WebSocket connected')
        
        stompClient.subscribe(`/topic/task-status/${route.params.id}`, (message) => {
          const data = JSON.parse(message.body)
          handleTaskStatusUpdate(data)
        })
      },
      onStompError: (frame) => {
        console.warn('WebSocket STOMP error:', frame.headers['message'])
        wsConnected = false
      },
      onWebSocketError: (error) => {
        console.warn('WebSocket connection error, using polling fallback:', error)
        wsConnected = false
      },
      onWebSocketClose: () => {
        wsConnected = false
        console.log('WebSocket disconnected')
      }
    })
    
    stompClient.activate()
  } catch (e) {
    console.warn('WebSocket setup failed, using polling fallback:', e)
  }
}

const disconnectWebSocket = () => {
  if (stompClient) {
    try {
      stompClient.deactivate()
    } catch (e) {
      console.warn('Error disconnecting WebSocket:', e)
    }
    wsConnected = false
    stompClient = null
  }
}

const handleTaskStatusUpdate = (data) => {
  if (!data || !data.taskId) return
  
  if (data.status === 'RUNNING') {
    if (task.value) {
      task.value.status = 'RUNNING'
      if (data.data?.startedAt) {
        task.value.startedAt = data.data.startedAt
      }
    }
  } else if (data.status === 'COMPLETED' || data.status === 'FAILED' || data.status === 'STOPPED') {
    loadTask()
  }
}

const getStatusType = (status) => {
  const typeMap = {
    PENDING: 'info',
    RUNNING: 'primary',
    COMPLETED: 'success',
    FAILED: 'danger',
    STOPPED: 'warning'
  }
  return typeMap[status] || 'info'
}

const getStatusText = (status) => {
  const textMap = {
    PENDING: '等待中',
    RUNNING: '运行中',
    COMPLETED: '已完成',
    FAILED: '失败',
    STOPPED: '已停止'
  }
  return textMap[status] || status
}

const getPriorityType = (priority) => {
  const typeMap = {
    LOW: 'info',
    MEDIUM: '',
    HIGH: 'warning',
    CRITICAL: 'danger'
  }
  return typeMap[priority] || ''
}

const getPriorityText = (priority) => {
  const textMap = {
    LOW: '低优先级',
    MEDIUM: '中优先级',
    HIGH: '高优先级',
    CRITICAL: '紧急优先级'
  }
  return textMap[priority] || priority || '中优先级'
}

const formatTime = (time) => {
  if (!time) return '-'
  return dayjs(time).format('YYYY-MM-DD HH:mm:ss')
}

const formatNumber = (value) => {
  if (value === null || value === undefined) return '0'
  return Number(value).toLocaleString()
}

const formatDecimal = (value, decimals = 2) => {
  if (value === null || value === undefined || isNaN(value)) return '-'
  return Number(value).toFixed(decimals)
}

const formatPercent = (value) => {
  if (value === null || value === undefined || isNaN(value)) return '0%'
  return Number(value).toFixed(2) + '%'
}

const loadTask = async () => {
  loading.value = true
  try {
    const res = await getTask(route.params.id)
    task.value = res.data
  } catch (error) {
    console.error('Failed to load task:', error)
  } finally {
    loading.value = false
  }
}

const loadChartData = async () => {
  if (!task.value) return

  const hasData = task.value.totalRequests && task.value.totalRequests > 0
  if (!hasData) return

  try {
    const [timelineRes, distributionRes] = await Promise.all([
      getTaskTimeline(route.params.id),
      getResponseTimeDistribution(route.params.id)
    ])

    await nextTick()

    renderResponseTimeChart(timelineRes.data || [])
    renderDistributionChart(distributionRes.data || [])
    renderThroughputChart(timelineRes.data || [])
    renderStatusChart()
  } catch (error) {
    console.error('Failed to load chart data:', error)
  }
}

const renderResponseTimeChart = (data) => {
  if (!responseTimeChartRef.value || !data || data.length === 0) return

  if (charts.responseTime) charts.responseTime.dispose()
  charts.responseTime = echarts.init(responseTimeChartRef.value)

  const validData = data.filter(d => d.elapsed !== null && d.elapsed !== undefined)
  if (validData.length === 0) return

  const maxPoints = 500
  const step = Math.max(1, Math.floor(validData.length / maxPoints))
  const sampledData = validData.filter((_, i) => i % step === 0)

  const option = {
    tooltip: {
      trigger: 'axis',
      formatter: (params) => {
        const p = params[0]
        return `索引: ${p.dataIndex}<br/>响应时间: ${p.value}ms`
      }
    },
    grid: { left: '3%', right: '4%', bottom: '3%', top: '10%', containLabel: true },
    xAxis: {
      type: 'category',
      data: sampledData.map((_, i) => i),
      axisLabel: { display: false },
      axisLine: { lineStyle: { color: '#e0e0e0' } }
    },
    yAxis: {
      type: 'value',
      name: '响应时间(ms)',
      nameTextStyle: { color: '#666' },
      axisLine: { lineStyle: { color: '#e0e0e0' } },
      splitLine: { lineStyle: { color: '#f0f0f0' } }
    },
    series: [{
      name: '响应时间',
      type: 'line',
      data: sampledData.map(d => d.elapsed),
      smooth: true,
      showSymbol: false,
      sampling: 'lttb',
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: 'rgba(64, 158, 255, 0.4)' },
          { offset: 1, color: 'rgba(64, 158, 255, 0.05)' }
        ])
      },
      lineStyle: { color: '#409eff', width: 2 }
    }]
  }

  charts.responseTime.setOption(option)
}

const renderDistributionChart = (data) => {
  if (!distributionChartRef.value || !data || data.length === 0) return

  if (charts.distribution) charts.distribution.dispose()
  charts.distribution = echarts.init(distributionChartRef.value)

  const validData = data.filter(d => d.count > 0)
  if (validData.length === 0) return

  const option = {
    tooltip: {
      trigger: 'axis',
      formatter: (params) => `${params[0].name}<br/>请求数量: ${params[0].value}`
    },
    grid: { left: '3%', right: '4%', bottom: '3%', top: '10%', containLabel: true },
    xAxis: {
      type: 'category',
      data: validData.map(d => d.range),
      axisLabel: {
        rotate: 30,
        fontSize: 11
      },
      axisLine: { lineStyle: { color: '#e0e0e0' } }
    },
    yAxis: {
      type: 'value',
      name: '请求数量',
      nameTextStyle: { color: '#666' },
      axisLine: { lineStyle: { color: '#e0e0e0' } },
      splitLine: { lineStyle: { color: '#f0f0f0' } }
    },
    series: [{
      type: 'bar',
      data: validData.map(d => d.count),
      barWidth: '60%',
      itemStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: '#67c23a' },
          { offset: 1, color: '#95d475' }
        ]),
        borderRadius: [4, 4, 0, 0]
      }
    }]
  }

  charts.distribution.setOption(option)
}

const renderThroughputChart = (data) => {
  if (!throughputChartRef.value || !data || data.length === 0) return

  if (charts.throughput) charts.throughput.dispose()
  charts.throughput = echarts.init(throughputChartRef.value)

  const secondGroups = {}
  data.forEach(item => {
    if (!item.timestamp) return
    const second = dayjs(item.timestamp).format('HH:mm:ss')
    if (!secondGroups[second]) {
      secondGroups[second] = 0
    }
    secondGroups[second]++
  })

  const labels = Object.keys(secondGroups)
  const values = Object.values(secondGroups)

  if (labels.length === 0) return

  const option = {
    tooltip: {
      trigger: 'axis',
      formatter: (params) => `时间: ${params[0].name}<br/>请求数: ${params[0].value}`
    },
    grid: { left: '3%', right: '4%', bottom: '3%', top: '10%', containLabel: true },
    xAxis: {
      type: 'category',
      data: labels,
      axisLabel: {
        rotate: 45,
        fontSize: 10
      },
      axisLine: { lineStyle: { color: '#e0e0e0' } }
    },
    yAxis: {
      type: 'value',
      name: '请求数/秒',
      nameTextStyle: { color: '#666' },
      axisLine: { lineStyle: { color: '#e0e0e0' } },
      splitLine: { lineStyle: { color: '#f0f0f0' } }
    },
    series: [{
      type: 'bar',
      data: values,
      barWidth: '70%',
      itemStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: '#e6a23c' },
          { offset: 1, color: '#f0c78a' }
        ]),
        borderRadius: [4, 4, 0, 0]
      }
    }]
  }

  charts.throughput.setOption(option)
}

const renderStatusChart = () => {
  if (!statusChartRef.value || !task.value) return

  if (charts.status) charts.status.dispose()
  charts.status = echarts.init(statusChartRef.value)

  const successCount = task.value.successCount || 0
  const failureCount = task.value.failureCount || 0
  const total = successCount + failureCount

  if (total === 0) {
    charts.status.setOption({
      title: {
        text: '暂无数据',
        left: 'center',
        top: 'center',
        textStyle: { color: '#999', fontSize: 14 }
      }
    })
    return
  }

  const option = {
    tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
    legend: { orient: 'vertical', right: '10%', top: 'center' },
    series: [{
      type: 'pie',
      radius: ['50%', '75%'],
      center: ['40%', '50%'],
      avoidLabelOverlap: false,
      itemStyle: { borderRadius: 8, borderColor: '#fff', borderWidth: 2 },
      label: { show: false },
      emphasis: {
        label: { show: true, fontSize: 16, fontWeight: 'bold' }
      },
      data: [
        { value: successCount, name: '成功', itemStyle: { color: '#67c23a' } },
        { value: failureCount, name: '失败', itemStyle: { color: '#f56c6c' } }
      ]
    }]
  }

  charts.status.setOption(option)
}

const handleStart = async () => {
  try {
    await startTask(task.value.id)
    ElMessage.success('任务启动成功')
    loadTask()
  } catch (error) {
    ElMessage.error('任务启动失败')
  }
}

const handleStop = async () => {
  try {
    await ElMessageBox.confirm('确定要停止该任务吗？', '停止确认', { type: 'warning' })
    await stopTask(task.value.id)
    ElMessage.success('任务已停止')
    loadTask()
  } catch (error) {
    if (error !== 'cancel') {
      console.error('Failed to stop task:', error)
    }
  }
}

const handleExportReport = async () => {
  reporting.value = true
  try {
    const res = await downloadReport(route.params.id, reportFormat.value)

    const blob = new Blob([res], {
      type: reportFormat.value === 'excel'
        ? 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'
        : 'text/html'
    })
    const url = window.URL.createObjectURL(blob)
    const link = document.createElement('a')
    link.href = url
    link.download = `report_${route.params.id}.${reportFormat.value === 'excel' ? 'xlsx' : 'html'}`
    link.click()
    window.URL.revokeObjectURL(url)

    ElMessage.success('报告导出成功')
    showReportDialog.value = false
  } catch (error) {
    console.error('Failed to export report:', error)
    ElMessage.error('报告导出失败')
  } finally {
    reporting.value = false
  }
}

const handleResize = () => {
  Object.values(charts).forEach(chart => chart?.resize())
}

onMounted(() => {
  loadTask()
  connectWebSocket()

  refreshInterval = setInterval(() => {
    if (task.value?.status === 'RUNNING') {
      loadTask()
    }
  }, 3000)

  window.addEventListener('resize', handleResize)
})

watch(task, (newTask) => {
  if (newTask && (newTask.status === 'COMPLETED' || newTask.status === 'FAILED' || newTask.status === 'STOPPED')) {
    loadChartData()
  }
})

onUnmounted(() => {
  disconnectWebSocket()
  if (refreshInterval) {
    clearInterval(refreshInterval)
  }
  Object.values(charts).forEach(chart => chart?.dispose())
  window.removeEventListener('resize', handleResize)
})
</script>

<style lang="scss" scoped>
.task-detail {
  .page-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 20px;
  }

  .header-left {
    display: flex;
    align-items: center;
    gap: 15px;

    .page-title {
      margin: 0;
      font-size: 20px;
      font-weight: 600;
    }
  }

  .metrics-row {
    margin-bottom: 20px;
  }

  .metric-card {
    background: #fff;
    border-radius: 8px;
    padding: 20px;
    text-align: center;
    box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);
    transition: transform 0.2s;

    &:hover {
      transform: translateY(-2px);
    }

    .metric-label {
      font-size: 14px;
      color: #666;
      margin-bottom: 10px;
    }

    .metric-value {
      font-size: 28px;
      font-weight: 700;
      color: #333;
    }

    &.metric-total .metric-value { color: #409eff; }
    &.metric-success .metric-value { color: #67c23a; }
    &.metric-failure .metric-value { color: #f56c6c; }
    &.metric-error .metric-value { color: #e6a23c; }
    &.metric-avg .metric-value { color: #909399; }
    &.metric-p95 .metric-value { color: #1f2937; }
    &.metric-p99 .metric-value { color: #1f2937; }
    &.metric-throughput .metric-value { color: #409eff; }
  }

  .chart-row {
    margin-bottom: 20px;
  }

  .card-container {
    background: #fff;
    border-radius: 8px;
    padding: 20px;
    box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);

    h3 {
      margin: 0 0 15px 0;
      font-size: 16px;
      font-weight: 600;
    }
  }

  .chart-container {
    height: 280px;
  }

  .mt-20 {
    margin-top: 20px;
  }
}
</style>
