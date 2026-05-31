<template>
  <div>
    <a-page-header title="审计数据看板" sub-title="OAuth2 授权统计概览" />
    
    <a-row :gutter="16" style="margin-top: 16px">
      <a-col :span="6">
        <a-statistic title="总请求次数" :value="stats.totalCalls || 0" :value-style="{ color: '#3f8600' }">
          <template #prefix><api-outlined /></template>
        </a-statistic>
      </a-col>
      <a-col :span="6">
        <a-statistic title="异常请求" :value="stats.anomalyCount || 0" :value-style="{ color: '#cf1322' }">
          <template #prefix><warning-outlined /></template>
        </a-statistic>
      </a-col>
      <a-col :span="6">
        <a-statistic title="活跃应用" :value="Object.keys(stats.topClients || {}).length">
          <template #prefix><appstore-outlined /></template>
        </a-statistic>
      </a-col>
      <a-col :span="6">
        <a-statistic title="活跃用户" :value="Object.keys(stats.topUsers || {}).length">
          <template #prefix><user-outlined /></template>
        </a-statistic>
      </a-col>
    </a-row>

    <a-row :gutter="16" style="margin-top: 24px">
      <a-col :span="12">
        <a-card title="Top 应用调用统计" style="height: 400px">
          <div ref="clientChartRef" style="width: 100%; height: 350px"></div>
        </a-card>
      </a-col>
      <a-col :span="12">
        <a-card title="Top 用户调用统计" style="height: 400px">
          <div ref="userChartRef" style="width: 100%; height: 350px"></div>
        </a-card>
      </a-col>
    </a-row>

    <a-row style="margin-top: 24px">
      <a-col :span="24">
        <a-card title="异常请求列表">
          <a-table :columns="anomalyColumns" :data-source="anomalyLogs" row-key="id" :loading="loading">
            <template #bodyCell="{ column, record }">
              <template v-if="column.key === 'anomalyType'">
                <a-tag color="red">{{ record.anomalyType }}</a-tag>
              </template>
            </template>
          </a-table>
        </a-card>
      </a-col>
    </a-row>
  </div>
</template>

<script setup>
import { ref, onMounted, nextTick } from 'vue'
import * as echarts from 'echarts'
import axios from 'axios'
import { ApiOutlined, WarningOutlined, AppstoreOutlined, UserOutlined } from '@ant-design/icons-vue'

const clientChartRef = ref(null)
const userChartRef = ref(null)
const stats = ref({})
const anomalyLogs = ref([])
const loading = ref(false)

const anomalyColumns = [
  { title: '时间', dataIndex: 'timestamp', key: 'timestamp' },
  { title: '用户', dataIndex: 'username', key: 'username' },
  { title: '应用', dataIndex: 'clientName', key: 'clientName' },
  { title: '资源', dataIndex: 'resourcePath', key: 'resourcePath' },
  { title: '异常类型', dataIndex: 'anomalyType', key: 'anomalyType' }
]

const fetchStats = async () => {
  try {
    const response = await axios.get('/api/audit/dashboard/stats')
    stats.value = response.data
    await nextTick()
    renderCharts()
  } catch (error) {
    console.error('Failed to fetch stats:', error)
  }
}

const fetchAnomalies = async () => {
  loading.value = true
  try {
    const response = await axios.get('/api/audit/logs/anomalies')
    anomalyLogs.value = response.data
  } catch (error) {
    console.error('Failed to fetch anomalies:', error)
  } finally {
    loading.value = false
  }
}

const renderCharts = () => {
  if (clientChartRef.value) {
    const clientChart = echarts.init(clientChartRef.value)
    const clientData = Object.entries(stats.value.topClients || {}).map(([name, count]) => ({
      name,
      value: count
    }))
    
    clientChart.setOption({
      tooltip: { trigger: 'item' },
      series: [{
        type: 'pie',
        radius: ['40%', '70%'],
        data: clientData,
        emphasis: {
          itemStyle: {
            shadowBlur: 10,
            shadowOffsetX: 0,
            shadowColor: 'rgba(0, 0, 0, 0.5)'
          }
        }
      }]
    })
  }

  if (userChartRef.value) {
    const userChart = echarts.init(userChartRef.value)
    const userData = Object.entries(stats.value.topUsers || {}).map(([name, count]) => ({
      name,
      value: count
    }))
    
    userChart.setOption({
      tooltip: { trigger: 'axis' },
      xAxis: { type: 'category', data: userData.map(d => d.name) },
      yAxis: { type: 'value' },
      series: [{
        type: 'bar',
        data: userData.map(d => d.value),
        itemStyle: { color: '#1890ff' }
      }]
    })
  }
}

onMounted(() => {
  fetchStats()
  fetchAnomalies()
})
</script>
