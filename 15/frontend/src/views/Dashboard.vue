<template>
  <div class="dashboard">
    <el-row :gutter="20" class="stat-row">
      <el-col :span="6">
        <div class="stat-card stat-card-blue">
          <div class="stat-icon">
            <el-icon :size="32"><Setting /></el-icon>
          </div>
          <div class="stat-info">
            <div class="stat-value">{{ stats.configCount }}</div>
            <div class="stat-label">压测配置数</div>
          </div>
        </div>
      </el-col>
      <el-col :span="6">
        <div class="stat-card stat-card-green">
          <div class="stat-icon">
            <el-icon :size="32"><List /></el-icon>
          </div>
          <div class="stat-info">
            <div class="stat-value">{{ stats.taskCount }}</div>
            <div class="stat-label">总任务数</div>
          </div>
        </div>
      </el-col>
      <el-col :span="6">
        <div class="stat-card stat-card-orange">
          <div class="stat-icon">
            <el-icon :size="32"><VideoPlay /></el-icon>
          </div>
          <div class="stat-info">
            <div class="stat-value">{{ stats.runningCount }}</div>
            <div class="stat-label">运行中任务</div>
          </div>
        </div>
      </el-col>
      <el-col :span="6">
        <div class="stat-card stat-card-red">
          <div class="stat-icon">
            <el-icon :size="32"><CircleCheck /></el-icon>
          </div>
          <div class="stat-info">
            <div class="stat-value">{{ stats.completedCount }}</div>
            <div class="stat-label">已完成任务</div>
          </div>
        </div>
      </el-col>
    </el-row>

    <el-row :gutter="20" class="mt-20">
      <el-col :span="12">
        <div class="card-container">
          <div class="card-header">
            <h3>最近任务</h3>
            <el-button type="primary" link @click="$router.push('/tasks')">查看全部</el-button>
          </div>
          <el-table :data="recentTasks" style="width: 100%" v-loading="loading">
            <el-table-column prop="name" label="任务名称" min-width="150" />
            <el-table-column prop="status" label="状态" width="100">
              <template #default="{ row }">
                <el-tag :type="getStatusType(row.status)" size="small">
                  {{ getStatusText(row.status) }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column prop="totalRequests" label="请求数" width="100">
              <template #default="{ row }">
                {{ row.totalRequests || '-' }}
              </template>
            </el-table-column>
            <el-table-column prop="avgResponseTime" label="平均响应(ms)" width="120">
              <template #default="{ row }">
                {{ row.avgResponseTime ? row.avgResponseTime.toFixed(2) : '-' }}
              </template>
            </el-table-column>
          </el-table>
        </div>
      </el-col>
      <el-col :span="12">
        <div class="card-container">
          <div class="card-header">
            <h3>任务状态分布</h3>
          </div>
          <div ref="chartRef" class="chart-container"></div>
        </div>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, shallowRef } from 'vue'
import { getTaskList } from '@/api/task'
import { getConfigList } from '@/api/config'
import * as echarts from 'echarts'

const loading = ref(false)
const recentTasks = ref([])
const stats = ref({
  configCount: 0,
  taskCount: 0,
  runningCount: 0,
  completedCount: 0
})

const chartRef = ref(null)
let chartInstance = null

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

const loadData = async () => {
  loading.value = true
  try {
    const [tasksRes, configsRes] = await Promise.all([
      getTaskList(),
      getConfigList()
    ])

    const tasks = tasksRes.data || []
    recentTasks.value = tasks.slice(0, 5)

    stats.value = {
      configCount: configsRes.data?.length || 0,
      taskCount: tasks.length,
      runningCount: tasks.filter(t => t.status === 'RUNNING').length,
      completedCount: tasks.filter(t => t.status === 'COMPLETED').length
    }

    renderChart(tasks)
  } catch (error) {
    console.error('Failed to load dashboard data:', error)
  } finally {
    loading.value = false
  }
}

const renderChart = (tasks) => {
  if (!chartRef.value) return

  if (chartInstance) {
    chartInstance.dispose()
  }

  chartInstance = echarts.init(chartRef.value)

  const statusCount = {
    PENDING: 0,
    RUNNING: 0,
    COMPLETED: 0,
    FAILED: 0,
    STOPPED: 0
  }

  tasks.forEach(task => {
    if (statusCount[task.status] !== undefined) {
      statusCount[task.status]++
    }
  })

  const option = {
    tooltip: {
      trigger: 'item',
      formatter: '{b}: {c} ({d}%)'
    },
    legend: {
      orient: 'vertical',
      right: '10%',
      top: 'center'
    },
    series: [
      {
        type: 'pie',
        radius: ['40%', '70%'],
        center: ['40%', '50%'],
        avoidLabelOverlap: false,
        itemStyle: {
          borderRadius: 10,
          borderColor: '#fff',
          borderWidth: 2
        },
        label: {
          show: false
        },
        emphasis: {
          label: {
            show: true,
            fontSize: 16,
            fontWeight: 'bold'
          }
        },
        data: [
          { value: statusCount.PENDING, name: '等待中', itemStyle: { color: '#909399' } },
          { value: statusCount.RUNNING, name: '运行中', itemStyle: { color: '#409eff' } },
          { value: statusCount.COMPLETED, name: '已完成', itemStyle: { color: '#67c23a' } },
          { value: statusCount.FAILED, name: '失败', itemStyle: { color: '#f56c6c' } },
          { value: statusCount.STOPPED, name: '已停止', itemStyle: { color: '#e6a23c' } }
        ]
      }
    ]
  }

  chartInstance.setOption(option)
}

const handleResize = () => {
  chartInstance?.resize()
}

onMounted(() => {
  loadData()
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  chartInstance?.dispose()
})
</script>

<style lang="scss" scoped>
.dashboard {
  .stat-row {
    .stat-card {
      display: flex;
      align-items: center;
      padding: 20px;
      border-radius: 8px;
      color: #fff;
      transition: transform 0.3s, box-shadow 0.3s;

      &:hover {
        transform: translateY(-5px);
        box-shadow: 0 10px 20px rgba(0, 0, 0, 0.15);
      }

      .stat-icon {
        width: 60px;
        height: 60px;
        display: flex;
        align-items: center;
        justify-content: center;
        background: rgba(255, 255, 255, 0.2);
        border-radius: 8px;
        margin-right: 15px;
      }

      .stat-info {
        .stat-value {
          font-size: 28px;
          font-weight: 700;
          line-height: 1.2;
        }

        .stat-label {
          font-size: 14px;
          opacity: 0.9;
          margin-top: 4px;
        }
      }
    }

    .stat-card-blue {
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    }

    .stat-card-green {
      background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%);
    }

    .stat-card-orange {
      background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);
    }

    .stat-card-red {
      background: linear-gradient(135deg, #fa709a 0%, #fee140 100%);
    }
  }

  .card-container {
    background: #fff;
    border-radius: 8px;
    padding: 20px;
    box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);
  }

  .card-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 15px;

    h3 {
      margin: 0;
      font-size: 16px;
      font-weight: 600;
      color: #1f2937;
    }
  }

  .chart-container {
    height: 300px;
  }
}
</style>
