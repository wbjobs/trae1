<template>
  <div class="report-list">
    <div class="page-header">
      <h2 class="page-title">报告导出</h2>
    </div>

    <div class="card-container">
      <el-alert
        title="提示"
        type="info"
        :closable="false"
        style="margin-bottom: 20px;"
      >
        <template #title>
          选择一个已完成的压测任务，生成并导出测试报告。支持 HTML、Excel 等多种格式。
        </template>
      </el-alert>

      <el-table :data="taskList" style="width: 100%" v-loading="loading">
        <el-table-column prop="name" label="任务名称" min-width="200" />
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
        <el-table-column label="操作" width="250" fixed="right">
          <template #default="{ row }">
            <el-button
              v-if="row.status === 'COMPLETED'"
              type="primary"
              link
              :icon="Document"
              @click="handleGenerate(row, 'html')"
            >HTML报告</el-button>
            <el-button
              v-if="row.status === 'COMPLETED'"
              type="success"
              link
              :icon="Excel"
              @click="handleGenerate(row, 'excel')"
            >Excel报告</el-button>
            <el-button
              v-if="row.status === 'COMPLETED'"
              type="warning"
              link
              :icon="View"
              @click="handleViewDetail(row)"
            >查看详情</el-button>
            <span v-else class="text-disabled">未完成</span>
          </template>
        </el-table-column>
      </el-table>
    </div>

    <el-dialog v-model="showDownloadDialog" title="报告生成成功" width="400px">
      <el-result icon="success" title="报告已生成" sub-title="点击下载按钮获取报告文件">
        <template #extra>
          <el-button type="primary" :icon="Download" @click="handleDownload">
            下载报告
          </el-button>
        </template>
      </el-result>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { Document, Excel, View, Download } from '@element-plus/icons-vue'
import { getTaskList, generateReport, downloadReport } from '@/api/task'
import dayjs from 'dayjs'

const router = useRouter()
const loading = ref(false)
const taskList = ref([])

const showDownloadDialog = ref(false)
const currentTaskId = ref(null)
const currentFormat = ref('html')

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
    taskList.value = res.data || []
  } catch (error) {
    console.error('Failed to load tasks:', error)
  } finally {
    loading.value = false
  }
}

const handleGenerate = async (row, format) => {
  currentTaskId.value = row.id
  currentFormat.value = format
  
  try {
    await generateReport(row.id, format)
    showDownloadDialog.value = true
  } catch (error) {
    console.error('Failed to generate report:', error)
    ElMessage.error('报告生成失败')
  }
}

const handleDownload = async () => {
  try {
    const res = await downloadReport(currentTaskId.value, currentFormat.value)
    
    const blob = new Blob([res], {
      type: currentFormat.value === 'excel'
        ? 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'
        : 'text/html'
    })
    const url = window.URL.createObjectURL(blob)
    const link = document.createElement('a')
    link.href = url
    link.download = `report_${currentTaskId.value}.${currentFormat.value === 'excel' ? 'xlsx' : 'html'}`
    link.click()
    window.URL.revokeObjectURL(url)
    
    ElMessage.success('报告下载成功')
    showDownloadDialog.value = false
  } catch (error) {
    console.error('Failed to download report:', error)
    ElMessage.error('报告下载失败')
  }
}

const handleViewDetail = (row) => {
  router.push(`/results/${row.id}`)
}

onMounted(() => {
  loadData()
})
</script>

<style lang="scss" scoped>
.report-list {
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

  .text-disabled {
    color: #999;
    font-size: 12px;
  }
}
</style>
