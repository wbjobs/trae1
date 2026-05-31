<template>
  <div class="result-detail">
    <div class="page-header">
      <div class="header-left">
        <el-button :icon="ArrowLeft" @click="$router.back()">返回</el-button>
        <h2 class="page-title">压测结果详情</h2>
      </div>
      <div class="header-right">
        <el-button type="primary" :icon="Download" @click="showReportDialog = true">
          导出报告
        </el-button>
      </div>
    </div>

    <div v-loading="loading" class="detail-content">
      <template v-if="task">
        <el-row :gutter="20" class="overview-row">
          <el-col :span="4">
            <div class="stat-card">
              <div class="stat-value">{{ task.totalRequests || 0 }}</div>
              <div class="stat-label">总请求数</div>
            </div>
          </el-col>
          <el-col :span="4">
            <div class="stat-card success">
              <div class="stat-value">{{ task.successCount || 0 }}</div>
              <div class="stat-label">成功</div>
            </div>
          </el-col>
          <el-col :span="4">
            <div class="stat-card danger">
              <div class="stat-value">{{ task.failureCount || 0 }}</div>
              <div class="stat-label">失败</div>
            </div>
          </el-col>
          <el-col :span="4">
            <div class="stat-card">
              <div class="stat-value">{{ task.avgResponseTime ? task.avgResponseTime.toFixed(2) : '-' }}</div>
              <div class="stat-label">平均响应(ms)</div>
            </div>
          </el-col>
          <el-col :span="4">
            <div class="stat-card">
              <div class="stat-value">{{ task.p95ResponseTime ? task.p95ResponseTime.toFixed(2) : '-' }}</div>
              <div class="stat-label">P95响应(ms)</div>
            </div>
          </el-col>
          <el-col :span="4">
            <div class="stat-card">
              <div class="stat-value">{{ task.errorRate ? task.errorRate.toFixed(2) + '%' : '-' }}</div>
              <div class="stat-label">错误率</div>
            </div>
          </el-col>
        </el-row>

        <el-row :gutter="20" class="chart-row">
          <el-col :span="12">
            <div class="card-container">
              <h3>响应时间趋势</h3>
              <div ref="responseTimeChartRef" class="chart-box"></div>
            </div>
          </el-col>
          <el-col :span="12">
            <div class="card-container">
              <h3>响应时间分布</h3>
              <div ref="distributionChartRef" class="chart-box"></div>
            </div>
          </el-col>
        </el-row>

        <el-row :gutter="20" class="chart-row">
          <el-col :span="24">
            <div class="card-container">
              <h3>每秒请求数趋势</h3>
              <div ref="qpsChartRef" class="chart-box"></div>
            </div>
          </el-col>
        </el-row>

        <div class="card-container">
          <h3>详细数据</h3>
          <el-table :data="timelineData" style="width: 100%" max-height="400" v-loading="dataLoading">
            <el-table-column prop="timestamp" label="时间" width="180">
              <template #default="{ row }">
                {{ formatTime(row.timestamp) }}
              </template>
            </el-table-column>
            <el-table-column prop="elapsed" label="响应时间(ms)" width="120" />
            <el-table-column prop="responseCode" label="状态码" width="100" />
            <el-table-column prop="success" label="结果" width="100">
              <template #default="{ row }">
                <el-tag :type="row.success ? 'success' : 'danger'" size="small">
                  {{ row.success ? '成功' : '失败' }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column prop="bytes" label="字节数" width="120" />
            <el-table-column prop="latency" label="延迟(ms)" width="120" />
            <el-table-column prop="allThreads" label="活跃线程" width="120" />
          </el-table>
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
import { ref, onMounted, onUnmounted } from 'vue'
import { useRoute } from 'vue-router'
import { ElMessage } from 'element-plus'
import { ArrowLeft, Download } from '@element-plus/icons-vue'
import {
  getTask,
  getTaskTimeline,
  getResponseTimeDistribution,
  downloadReport
} from '@/api/task'
import * as echarts from 'echarts'
import dayjs from 'dayjs'

const route = useRoute()

const loading = ref(false)
const dataLoading = ref(false)
const task = ref(null)
const timelineData = ref([])
const distributionData = ref([])

const showReportDialog = ref(false)
const reportFormat = ref('html')
const reporting = ref(false)

const responseTimeChartRef = ref(null)
const distributionChartRef = ref(null)
const qpsChartRef = ref(null)

let charts = {}

const formatTime = (time) => {
  if (!time) return '-'
  return dayjs(time).format('YYYY-MM-DD HH:mm:ss')
}

const loadData = async () => {
  loading.value = true
  dataLoading.value = true
  try {
    const [taskRes, timelineRes, distributionRes] = await Promise.all([
      getTask(route.params.id),
      getTaskTimeline(route.params.id),
      getResponseTimeDistribution(route.params.id)
    ])

    task.value = taskRes.data
    timelineData.value = timelineRes.data || []
    distributionData.value = distributionRes.data || []

    renderResponseTimeChart()
    renderDistributionChart()
    renderQPSChart()
  } catch (error) {
    console.error('Failed to load data:', error)
  } finally {
    loading.value = false
    dataLoading.value = false
  }
}

const renderResponseTimeChart = () => {
  if (!responseTimeChartRef.value) return

  if (charts.responseTime) charts.responseTime.dispose()
  charts.responseTime = echarts.init(responseTimeChartRef.value)

  const data = timelineData.value

  const option = {
    tooltip: { trigger: 'axis' },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: {
      type: 'category',
      data: data.map((_, i) => i),
      axisLabel: { display: false }
    },
    yAxis: { type: 'value', name: '响应时间(ms)' },
    series: [{
      name: '响应时间',
      type: 'line',
      data: data.map(d => d.elapsed),
      smooth: true,
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: 'rgba(64, 158, 255, 0.5)' },
          { offset: 1, color: 'rgba(64, 158, 255, 0.05)' }
        ])
      },
      lineStyle: { color: '#409eff', width: 2 }
    }]
  }

  charts.responseTime.setOption(option)
}

const renderDistributionChart = () => {
  if (!distributionChartRef.value) return

  if (charts.distribution) charts.distribution.dispose()
  charts.distribution = echarts.init(distributionChartRef.value)

  const option = {
    tooltip: { trigger: 'axis' },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: {
      type: 'category',
      data: distributionData.value.map(d => d.range),
      axisLabel: { rotate: 30 }
    },
    yAxis: { type: 'value', name: '请求数量' },
    series: [{
      type: 'bar',
      data: distributionData.value.map(d => d.count),
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

const renderQPSChart = () => {
  if (!qpsChartRef.value) return

  if (charts.qps) charts.qps.dispose()
  charts.qps = echarts.init(qpsChartRef.value)

  const data = timelineData.value
  const secondGroups = {}
  
  data.forEach(item => {
    if (!item.timestamp) return
    const second = dayjs(item.timestamp).format('HH:mm:ss')
    if (!secondGroups[second]) {
      secondGroups[second] = 0
    }
    secondGroups[second]++
  })

  const option = {
    tooltip: { trigger: 'axis' },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: {
      type: 'category',
      data: Object.keys(secondGroups),
      axisLabel: { rotate: 45 }
    },
    yAxis: { type: 'value', name: '请求数/秒' },
    series: [{
      type: 'bar',
      data: Object.values(secondGroups),
      itemStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: '#e6a23c' },
          { offset: 1, color: '#f0c78a' }
        ]),
        borderRadius: [4, 4, 0, 0]
      }
    }]
  }

  charts.qps.setOption(option)
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
  loadData()
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  Object.values(charts).forEach(chart => chart?.dispose())
  window.removeEventListener('resize', handleResize)
})
</script>

<style lang="scss" scoped>
.result-detail {
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

  .overview-row {
    margin-bottom: 20px;
  }

  .stat-card {
    background: #fff;
    border-radius: 8px;
    padding: 20px;
    text-align: center;
    box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);

    .stat-value {
      font-size: 24px;
      font-weight: 700;
      color: #409eff;
      margin-bottom: 8px;
    }

    .stat-label {
      font-size: 14px;
      color: #666;
    }

    &.success .stat-value { color: #67c23a; }
    &.danger .stat-value { color: #f56c6c; }
  }

  .chart-row {
    margin-bottom: 20px;
  }

  .card-container {
    background: #fff;
    border-radius: 8px;
    padding: 20px;
    box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);
    margin-bottom: 20px;

    h3 {
      margin: 0 0 15px 0;
      font-size: 16px;
      font-weight: 600;
    }
  }

  .chart-box {
    height: 300px;
  }
}
</style>
