<template>
  <div class="config-list">
    <div class="page-header">
      <h2 class="page-title">压测配置管理</h2>
      <el-button type="primary" :icon="Plus" @click="$router.push('/configs/create')">
        新建配置
      </el-button>
    </div>

    <div class="card-container">
      <el-table :data="configList" style="width: 100%" v-loading="loading">
        <el-table-column prop="name" label="配置名称" min-width="150" />
        <el-table-column prop="method" label="请求方法" width="100">
          <template #default="{ row }">
            <el-tag :type="getMethodType(row.method)" size="small">{{ row.method }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="url" label="请求地址" min-width="200" show-overflow-tooltip />
        <el-table-column prop="threadCount" label="线程数" width="100" />
        <el-table-column prop="rampUpTime" label="启动时间(s)" width="100" />
        <el-table-column label="运行设置" width="150">
          <template #default="{ row }">
            {{ row.useLoopCount ? `循环 ${row.loopCount} 次` : `持续 ${row.duration} 秒` }}
          </template>
        </el-table-column>
        <el-table-column prop="createdAt" label="创建时间" width="180">
          <template #default="{ row }">
            {{ formatTime(row.createdAt) }}
          </template>
        </el-table-column>
        <el-table-column label="操作" width="200" fixed="right">
          <template #default="{ row }">
            <el-button type="primary" link :icon="Edit" @click="handleEdit(row)">编辑</el-button>
            <el-button type="success" link :icon="VideoPlay" @click="handleStart(row)">启动</el-button>
            <el-button type="danger" link :icon="Delete" @click="handleDelete(row)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Plus, Edit, Delete, VideoPlay } from '@element-plus/icons-vue'
import { getConfigList, deleteConfig } from '@/api/config'
import { createTask, startTask } from '@/api/task'
import dayjs from 'dayjs'

const router = useRouter()
const loading = ref(false)
const configList = ref([])

const getMethodType = (method) => {
  const typeMap = {
    GET: 'success',
    POST: 'warning',
    PUT: 'primary',
    DELETE: 'danger',
    PATCH: 'info',
    HEAD: 'info',
    OPTIONS: 'info'
  }
  return typeMap[method] || 'info'
}

const formatTime = (time) => {
  if (!time) return '-'
  return dayjs(time).format('YYYY-MM-DD HH:mm:ss')
}

const loadData = async () => {
  loading.value = true
  try {
    const res = await getConfigList()
    configList.value = res.data || []
  } catch (error) {
    console.error('Failed to load configs:', error)
  } finally {
    loading.value = false
  }
}

const handleEdit = (row) => {
  router.push(`/configs/edit/${row.id}`)
}

const handleStart = async (row) => {
  try {
    const taskRes = await createTask({ configId: row.id, name: `${row.name} - 压测任务` })
    if (taskRes.data) {
      await startTask(taskRes.data.id)
      ElMessage.success('任务启动成功')
      router.push(`/tasks/${taskRes.data.id}`)
    }
  } catch (error) {
    ElMessage.error('任务启动失败')
  }
}

const handleDelete = async (row) => {
  try {
    await ElMessageBox.confirm('确定要删除该配置吗？', '删除确认', {
      type: 'warning'
    })
    await deleteConfig(row.id)
    ElMessage.success('删除成功')
    loadData()
  } catch (error) {
    if (error !== 'cancel') {
      console.error('Failed to delete config:', error)
    }
  }
}

onMounted(() => {
  loadData()
})
</script>

<style lang="scss" scoped>
.config-list {
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
}
</style>
