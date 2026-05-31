<template>
  <div class="result-list">
    <div class="page-header">
      <h2 class="page-title">压测结果</h2>
    </div>

    <div class="card-container">
      <el-table :data="taskList" style="width: 100%" v-loading="loading" @row-click="handleRowClick">
        <el-table-column prop="name" label="任务名称" min-width="180" />
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
        <el-table-column prop="p95ResponseTime" label="P95响应(ms)" width="120">
          <template #default="{ row }">
            {{ row.p95ResponseTime ? row.p95ResponseTime.toFixed(2) : '-' }}
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
        <el-table-column prop="createdAt" label="执行时间" width="160">
          <template #default="{ row }">
            {{ formatTime(row.createdAt) }}
          </template>
        </el-table-column>
      </el-table>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { getTaskList } from '@/api/task'
import dayjs from 'dayjs'

const router = useRouter()
const loading = ref(false)
const taskList = ref([])

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
  return dayjs(time).format('YYYY-MM-DD HH:mm:ss')
}

const loadData = async () => {
  loading.value = true
  try {
    const res = await getTaskList()
    taskList.value = (res.data || []).filter(t => t.status === 'COMPLETED' || t.status === 'FAILED')
  } catch (error) {
    console.error('Failed to load results:', error)
  } finally {
    loading.value = false
  }
}

const handleRowClick = (row) => {
  router.push(`/results/${row.id}`)
}

onMounted(() => {
  loadData()
})
</script>

<style lang="scss" scoped>
.result-list {
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

    :deep(.el-table__row) {
      cursor: pointer;

      &:hover {
        background: #f5f7fa !important;
      }
    }
  }
}
</style>
