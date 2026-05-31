<template>
  <div class="task-compare">
    <div class="page-header">
      <div class="header-left">
        <el-button :icon="ArrowLeft" text @click="$router.back()">返回</el-button>
        <h2 class="page-title">压测任务对比</h2>
      </div>
      <div class="header-right">
        <el-button type="primary" :icon="Refresh" @click="loadData">
          刷新数据
        </el-button>
      </div>
    </div>

    <div class="selector-section card-container">
      <h3>选择要对比的任务</h3>
      <el-select
        v-model="selectedTaskIds"
        multiple
        collapse-tags
        collapse-tags-tooltip
        placeholder="请选择要对比的任务（最多4个）"
        style="width: 100%"
        :filterable="true"
        :max-collapse-tags="4"
        @change="handleTaskSelectionChange"
      >
        <el-option
          v-for="task in availableTasks"
          :key="task.id"
          :label="`${task.name} - ${getStatusText(task.status)}`"
          :value="task.id"
          :disabled="selectedTaskIds.length >= 4 && !selectedTaskIds.includes(task.id)"
        />
      </el-select>
      <el-button
        v-if="selectedTaskIds.length >= 2"
        type="success"
        class="mt-10"
        :loading="loading"
        @click="loadComparisonData"
      >
        开始对比
      </el-button>
    </div>

    <div v-if="comparisonData && comparisonData.tasks" class="comparison-content">
      <div class="card-container">
        <h3>任务指标对比</h3>
        <el-table :data="comparisonRows" style="width: 100%" border>
          <el-table-column prop="metric" label="指标" width="180" fixed />
          <el-table-column
            v-for="task in comparisonData.tasks"
            :key="task.id"
            :label="task.name"
            :prop="'task_' + task.id"
          >
            <template #default="{ row }">
              <span :class="getMetricClass(row.metric, row['task_' + task.id], task)">
                {{ formatMetricValue(row.metric, row['task_' + task.id]) }}
              </span>
            </template>
          </el-table-column>
        </el-table>
      </div>

      <div v-if="comparisonData.summary" class="card-container mt-20">
        <h3>变化趋势（第一个任务 vs 最后一个任务）</h3>
        <el-row :gutter="20">
          <el-col :span="6">
            <div class="change-card" :class="getChangeClass(comparisonData.summary.avgResponseTimeChange)">
              <div class="change-label">平均响应时间变化</div>
              <div class="change-value">{{ formatChange(comparisonData.summary.avgResponseTimeChange) }}</div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="change-card" :class="getChangeClass(comparisonData.summary.throughputChange, true)">
              <div class="change-label">吞吐量变化</div>
              <div class="change-value">{{ formatChange(comparisonData.summary.throughputChange) }}</div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="change-card" :class="getChangeClass(comparisonData.summary.errorRateChange)">
              <div class="change-label">错误率变化</div>
              <div class="change-value">{{ formatChange(comparisonData.summary.errorRateChange) }}</div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="change-card" :class="getChangeClass(comparisonData.summary.p95ResponseTimeChange)">
              <div class="change-label">P95响应时间变化</div>
              <div class="change-value">{{ formatChange(comparisonData.summary.p95ResponseTimeChange) }}</div>
            </div>
          </el-col>
        </el-row>
      </div>

      <el-row :gutter="20" class="mt-20">
        <el-col :span="12">
          <div class="card-container">
            <h3>响应时间对比</h3>
            <div ref="responseTimeChartRef" class="chart-container"></div>
          </div>
        </el-col>
        <el-col :span="12">
          <div class="card-container">
            <h3>吞吐量对比</h3>
            <div ref="throughputChartRef" class="chart-container"></div>
          </div>
        </el-col>
      </el-row>

      <el-row :gutter="20" class="mt-20">
        <el-col :span="12">
          <div class="card-container">
            <h3>错误率对比</h3>
            <div ref="errorRateChartRef" class="chart-container"></div>
          </div>
        </el-col>
        <el-col :span="12">
          <div class="card-container">
            <h3>请求数对比</h3>
            <div ref="requestsChartRef" class="chart-container"></div>
          </div>
        </el-col>
      </el-row>
    </div>

    <div v-else-if="!loading && selectedTaskIds.length < 2" class="empty-tip card-container">
      <el-empty description="请选择至少2个任务进行对比" />
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, nextTick, watch } from 'vue'
import { useRoute } from 'vue-router'
import { ArrowLeft, Refresh } from '@element-plus/icons-vue'
import { getTaskList, compareTasks } from '@/api/task'
import * as echarts from 'echarts'

const route = useRoute()

const loading = ref(false)
const availableTasks = ref([])
const selectedTaskIds = ref([])
const comparisonData = ref(null)
const comparisonRows = ref([])

const responseTimeChartRef = ref(null)
const throughputChartRef = ref(null)
const errorRateChartRef = ref(null)
const requestsChartRef = ref(null)

let charts = {}

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

const formatMetricValue = (metric, value) => {
  if (value === null || value === undefined) return '-'
  if (metric.includes('时间') || metric.includes('响应')) {
    return Number(value).toFixed(2) + ' ms'
  }
  if (metric.includes('吞吐量')) {
    return Number(value).toFixed(2) + ' req/s'
  }
  if (metric.includes('错误率')) {
    return Number(value).toFixed(2) + '%'
  }
  return Number(value).toLocaleString()
}

const formatChange = (value) => {
  if (value === null || value === undefined) return '-'
  const sign = value > 0 ? '+' : ''
  return `${sign}${value.toFixed(2)}%`
}

const getChangeClass = (value, inverse = false) => {
  if (value === null || value === undefined) return ''
  if (inverse) {
    return value >= 0 ? 'change-positive' : 'change-negative'
  }
  return value <= 0 ? 'change-positive' : 'change-negative'
}

const getMetricClass = (metric, value, task) => {
  if (metric.includes('错误率') && value > 10) return 'text-danger'
  return ''
}

const loadTasks = async () => {
  try {
    const res = await getTaskList()
    availableTasks.value = (res.data || []).filter(t => 
      t.status === 'COMPLETED' || t.status === 'FAILED' || t.status === 'STOPPED'
    )
  } catch (error) {
    console.error('Failed to load tasks:', error)
  }
}

const handleTaskSelectionChange = () => {
  if (selectedTaskIds.value.length > 4) {
    selectedTaskIds.value = selectedTaskIds.value.slice(0, 4)
  }
}

const loadComparisonData = async () => {
  if (selectedTaskIds.value.length < 2) return

  loading.value = true
  try {
    const res = await compareTasks(selectedTaskIds.value)
    comparisonData.value = res.data
    buildComparisonRows()
    await nextTick()
    renderCharts()
  } catch (error) {
    console.error('Failed to load comparison data:', error)
  } finally {
    loading.value = false
  }
}

const buildComparisonRows = () => {
  if (!comparisonData.value?.tasks) return

  const metrics = [
    { key: 'totalRequests', label: '总请求数' },
    { key: 'successCount', label: '成功数' },
    { key: 'failureCount', label: '失败数' },
    { key: 'avgResponseTime', label: '平均响应时间' },
    { key: 'minResponseTime', label: '最小响应时间' },
    { key: 'maxResponseTime', label: '最大响应时间' },
    { key: 'p95ResponseTime', label: 'P95响应时间' },
    { key: 'p99ResponseTime', label: 'P99响应时间' },
    { key: 'throughput', label: '吞吐量' },
    { key: 'errorRate', label: '错误率' }
  ]

  comparisonRows.value = metrics.map(metric => {
    const row = { metric: metric.label }
    comparisonData.value.tasks.forEach(task => {
      row['task_' + task.id] = task[metric.key]
    })
    return row
  })
}

const renderCharts = () => {
  if (!comparisonData.value?.tasks) return

  const tasks = comparisonData.value.tasks
  const taskNames = tasks.map(t => t.name)

  renderResponseTimeChart(tasks, taskNames)
  renderThroughputChart(tasks, taskNames)
  renderErrorRateChart(tasks, taskNames)
  renderRequestsChart(tasks, taskNames)
}

const renderResponseTimeChart = (tasks, taskNames) => {
  if (!responseTimeChartRef.value) return

  if (charts.responseTime) charts.responseTime.dispose()
  charts.responseTime = echarts.init(responseTimeChartRef.value)

  const option = {
    tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
    legend: { data: ['平均', 'P95', 'P99'] },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: { type: 'category', data: taskNames, axisLabel: { rotate: 30 } },
    yAxis: { type: 'value', name: '响应时间(ms)' },
    series: [
      { name: '平均', type: 'bar', data: tasks.map(t => t.avgResponseTime || 0), itemStyle: { color: '#409eff' } },
      { name: 'P95', type: 'bar', data: tasks.map(t => t.p95ResponseTime || 0), itemStyle: { color: '#e6a23c' } },
      { name: 'P99', type: 'bar', data: tasks.map(t => t.p99ResponseTime || 0), itemStyle: { color: '#f56c6c' } }
    ]
  }

  charts.responseTime.setOption(option)
}

const renderThroughputChart = (tasks, taskNames) => {
  if (!throughputChartRef.value) return

  if (charts.throughput) charts.throughput.dispose()
  charts.throughput = echarts.init(throughputChartRef.value)

  const option = {
    tooltip: { trigger: 'axis' },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: { type: 'category', data: taskNames, axisLabel: { rotate: 30 } },
    yAxis: { type: 'value', name: '吞吐量(req/s)' },
    series: [{
      type: 'bar',
      data: tasks.map(t => t.throughput || 0),
      itemStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: '#67c23a' },
          { offset: 1, color: '#95d475' }
        ])
      }
    }]
  }

  charts.throughput.setOption(option)
}

const renderErrorRateChart = (tasks, taskNames) => {
  if (!errorRateChartRef.value) return

  if (charts.errorRate) charts.errorRate.dispose()
  charts.errorRate = echarts.init(errorRateChartRef.value)

  const option = {
    tooltip: { trigger: 'axis', formatter: '{b}: {c}%' },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: { type: 'category', data: taskNames, axisLabel: { rotate: 30 } },
    yAxis: { type: 'value', name: '错误率(%)', max: 100 },
    series: [{
      type: 'bar',
      data: tasks.map(t => t.errorRate || 0),
      itemStyle: {
        color: (params) => {
          const value = params.value
          if (value < 5) return '#67c23a'
          if (value < 10) return '#e6a23c'
          return '#f56c6c'
        }
      }
    }]
  }

  charts.errorRate.setOption(option)
}

const renderRequestsChart = (tasks, taskNames) => {
  if (!requestsChartRef.value) return

  if (charts.requests) charts.requests.dispose()
  charts.requests = echarts.init(requestsChartRef.value)

  const option = {
    tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
    legend: { data: ['成功', '失败'] },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: { type: 'category', data: taskNames, axisLabel: { rotate: 30 } },
    yAxis: { type: 'value', name: '请求数' },
    series: [
      { name: '成功', type: 'bar', stack: 'total', data: tasks.map(t => t.successCount || 0), itemStyle: { color: '#67c23a' } },
      { name: '失败', type: 'bar', stack: 'total', data: tasks.map(t => t.failureCount || 0), itemStyle: { color: '#f56c6c' } }
    ]
  }

  charts.requests.setOption(option)
}

const handleResize = () => {
  Object.values(charts).forEach(chart => chart?.resize())
}

onMounted(() => {
  loadTasks()
  
  if (route.query.taskIds) {
    selectedTaskIds.value = route.query.taskIds.split(',').map(Number)
    if (selectedTaskIds.value.length >= 2) {
      loadComparisonData()
    }
  }

  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  Object.values(charts).forEach(chart => chart?.dispose())
  window.removeEventListener('resize', handleResize)
})
</script>

<style lang="scss" scoped>
.task-compare {
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

  .selector-section {
    margin-bottom: 20px;
  }

  .mt-10 {
    margin-top: 10px;
  }

  .mt-20 {
    margin-top: 20px;
  }

  .chart-container {
    height: 300px;
  }

  .change-card {
    background: #fff;
    border-radius: 8px;
    padding: 20px;
    text-align: center;
    box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);

    .change-label {
      font-size: 14px;
      color: #666;
      margin-bottom: 10px;
    }

    .change-value {
      font-size: 24px;
      font-weight: 700;
    }

    &.change-positive .change-value {
      color: #67c23a;
    }

    &.change-negative .change-value {
      color: #f56c6c;
    }
  }

  .text-danger {
    color: #f56c6c;
    font-weight: 600;
  }

  .empty-tip {
    padding: 60px 20px;
  }
}
</style>
