<template>
  <div class="task-list">
    <div class="page-header">
      <h2 class="page-title">任务管理</h2>
      <div>
        <el-select
          v-model="statusFilter"
          placeholder="筛选状态"
          clearable
          style="width: 150px; margin-right: 10px;"
          @change="loadData"
        >
          <el-option label="等待中" value="PENDING" />
          <el-option label="运行中" value="RUNNING" />
          <el-option label="已完成" value="COMPLETED" />
          <el-option label="失败" value="FAILED" />
          <el-option label="已停止" value="STOPPED" />
        </el-select>
        <el-button 
          type="primary" 
          :icon="DataAnalysis" 
          :disabled="selectedForCompare.length < 2"
          @click="handleCompare"
          style="margin-right: 10px;"
        >
          对比分析 ({{ selectedForCompare.length }})
        </el-button>
        <el-button type="primary" :icon="Plus" @click="$router.push('/tasks/create')">
          新建任务
        </el-button>
      </div>
    </div>

    <div class="card-container">
      <el-table :data="filteredTasks" style="width: 100%" v-loading="loading">
        <el-table-column type="selection" width="50" @selection-change="handleSelectionChange" />
        <el-table-column prop="name" label="任务名称" min-width="180" />
        <el-table-column prop="priority" label="优先级" width="90">
          <template #default="{ row }">
            <el-tag :type="getPriorityType(row.priority)" size="small">
              {{ getPriorityText(row.priority) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="status" label="状态" width="100">
          <template #default="{ row }">
            <el-tag :type="getStatusType(row.status)" size="small">
              {{ getStatusText(row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="totalRequests" label="总请求数" width="110">
          <template #default="{ row }">
            {{ row.totalRequests || '-' }}
          </template>
        </el-table-column>
        <el-table-column label="成功/失败" width="120">
          <template #default="{ row }">
            <span class="success-text">{{ row.successCount || 0 }}</span>
            /
            <span class="failure-text">{{ row.failureCount || 0 }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="avgResponseTime" label="平均响应(ms)" width="130">
          <template #default="{ row }">
            {{ row.avgResponseTime ? row.avgResponseTime.toFixed(2) : '-' }}
          </template>
        </el-table-column>
        <el-table-column prop="throughput" label="吞吐量" width="100">
          <template #default="{ row }">
            {{ row.throughput ? row.throughput.toFixed(2) + '/s' : '-' }}
          </template>
        </el-table-column>
        <el-table-column prop="errorRate" label="错误率" width="100">
          <template #default="{ row }">
            {{ row.errorRate !== undefined ? row.errorRate.toFixed(2) + '%' : '-' }}
          </template>
        </el-table-column>
        <el-table-column prop="createdAt" label="创建时间" width="160">
          <template #default="{ row }">
            {{ formatTime(row.createdAt) }}
          </template>
        </el-table-column>
        <el-table-column label="操作" width="250" fixed="right">
          <template #default="{ row }">
            <el-button type="primary" link :icon="View" @click="handleView(row)">查看</el-button>
            <el-button
              v-if="row.status === 'PENDING' || row.status === 'FAILED' || row.status === 'STOPPED'"
              type="success"
              link
              :icon="VideoPlay"
              @click="handleStart(row)"
            >启动</el-button>
            <el-button
              v-if="row.status === 'RUNNING'"
              type="warning"
              link
              :icon="VideoPause"
              @click="handleStop(row)"
            >停止</el-button>
            <el-button type="danger" link :icon="Delete" @click="handleDelete(row)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Plus, View, Delete, VideoPlay, VideoPause, DataAnalysis } from '@element-plus/icons-vue'
import { getTaskList, startTask, stopTask, deleteTask } from '@/api/task'
import dayjs from 'dayjs'
import { Client } from '@stomp/stompjs'
import SockJS from 'sockjs-client'

const router = useRouter()
const loading = ref(false)
const tasks = ref([])
const statusFilter = ref('')
const selectedForCompare = ref([])

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
        console.log('TaskList WebSocket connected')
        
        stompClient.subscribe('/topic/task-updates', (message) => {
          loadData()
        })
      },
      onStompError: (frame) => {
        console.warn('TaskList WebSocket STOMP error:', frame.headers['message'])
        wsConnected = false
      },
      onWebSocketError: (error) => {
        console.warn('TaskList WebSocket connection error:', error)
        wsConnected = false
      },
      onWebSocketClose: () => {
        wsConnected = false
      }
    })
    
    stompClient.activate()
  } catch (e) {
    console.warn('TaskList WebSocket setup failed:', e)
  }
}

const disconnectWebSocket = () => {
  if (stompClient) {
    try {
      stompClient.deactivate()
    } catch (e) {
      console.warn('Error disconnecting TaskList WebSocket:', e)
    }
    wsConnected = false
    stompClient = null
  }
}

const filteredTasks = computed(() => {
  if (!statusFilter.value) return tasks.value
  return tasks.value.filter(task => task.status === statusFilter.value)
})

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

const formatTime = (time) => {
  if (!time) return '-'
  return dayjs(time).format('MM-DD HH:mm:ss')
}

const loadData = async () => {
  try {
    const res = await getTaskList()
    tasks.value = res.data || []
  } catch (error) {
    console.error('Failed to load tasks:', error)
  }
}

const handleView = (row) => {
  router.push(`/tasks/${row.id}`)
}

const handleStart = async (row) => {
  try {
    await startTask(row.id)
    ElMessage.success('任务启动成功')
    loadData()
  } catch (error) {
    ElMessage.error('任务启动失败')
  }
}

const handleStop = async (row) => {
  try {
    await ElMessageBox.confirm('确定要停止该任务吗？', '停止确认', {
      type: 'warning'
    })
    await stopTask(row.id)
    ElMessage.success('任务已停止')
    loadData()
  } catch (error) {
    if (error !== 'cancel') {
      console.error('Failed to stop task:', error)
    }
  }
}

const handleDelete = async (row) => {
  try {
    await ElMessageBox.confirm('确定要删除该任务吗？删除后无法恢复。', '删除确认', {
      type: 'warning'
    })
    await deleteTask(row.id)
    ElMessage.success('删除成功')
    loadData()
  } catch (error) {
    if (error !== 'cancel') {
      console.error('Failed to delete task:', error)
    }
  }
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
    LOW: '低',
    MEDIUM: '中',
    HIGH: '高',
    CRITICAL: '紧急'
  }
  return textMap[priority] || priority || '中'
}

const handleSelectionChange = (selection) => {
  selectedForCompare.value = selection.filter(s => 
    s.status === 'COMPLETED' || s.status === 'FAILED' || s.status === 'STOPPED'
  )
}

const handleCompare = () => {
  if (selectedForCompare.value.length >= 2) {
    const taskIds = selectedForCompare.value.map(s => s.id)
    router.push({ path: '/tasks/compare', query: { taskIds: taskIds.join(',') } })
  }
}

onMounted(() => {
  loadData()
  loading.value = false
  connectWebSocket()
  refreshInterval = setInterval(() => {
    const hasRunning = tasks.value.some(t => t.status === 'RUNNING')
    if (hasRunning) {
      loadData()
    }
  }, 3000)
})

onUnmounted(() => {
  disconnectWebSocket()
  if (refreshInterval) {
    clearInterval(refreshInterval)
  }
})
</script>

<style lang="scss" scoped>
.task-list {
  .page-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 20px;
  }

  .page-title {
    margin: 0;
    font-size: 20px;
    font-weight: 600;
  }

  .card-container {
    background: #fff;
    border-radius: 8px;
    padding: 20px;
    box-shadow: 0 2px 12px rgba(0, 0, 0, 0.08);
  }

  .success-text {
    color: #67c23a;
    font-weight: 600;
  }

  .failure-text {
    color: #f56c6c;
    font-weight: 600;
  }
}
</style>
