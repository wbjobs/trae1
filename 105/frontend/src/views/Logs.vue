<template>
  <div>
    <a-page-header title="审计日志" sub-title="查询和分析 OAuth2 API 调用日志" />

    <a-card style="margin-top: 16px">
      <a-form layout="inline" style="margin-bottom: 16px">
        <a-form-item label="查询类型">
          <a-select v-model:value="queryType" placeholder="选择查询类型" style="width: 150px" @change="resetQuery">
            <a-select-option value="range">时间范围</a-select-option>
            <a-select-option value="user">按用户</a-select-option>
            <a-select-option value="client">按应用</a-select-option>
            <a-select-option value="anomalies">异常请求</a-select-option>
          </a-select>
        </a-form-item>
        
        <a-form-item label="用户ID" v-if="queryType === 'user'">
          <a-input v-model:value="userId" placeholder="输入用户ID" style="width: 150px" />
        </a-form-item>
        
        <a-form-item label="应用ID" v-if="queryType === 'client'">
          <a-input v-model:value="clientId" placeholder="输入应用ID" style="width: 150px" />
        </a-form-item>
        
        <a-range-picker
          v-model:value="dateRange"
          v-if="queryType === 'range'"
          show-time
          format="YYYY-MM-DD HH:mm:ss"
          style="width: 400px"
        />
        
        <a-form-item>
          <a-button type="primary" @click="fetchLogs">
            <template #icon><search-outlined /></template>
            查询
          </a-button>
        </a-form-item>
      </a-form>

      <a-table :columns="columns" :data-source="logs" row-key="id" :loading="loading" :pagination="{ pageSize: 10 }">
        <template #bodyCell="{ column, record }">
          <template v-if="column.key === 'isAnomaly'">
            <a-tag :color="record.isAnomaly ? 'red' : 'green'">
              {{ record.isAnomaly ? '异常' : '正常' }}
            </a-tag>
          </template>
        </template>
      </a-table>
    </a-card>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import axios from 'axios'
import { SearchOutlined } from '@ant-design/icons-vue'
import { message } from 'ant-design-vue'

const queryType = ref('range')
const userId = ref('')
const clientId = ref('')
const dateRange = ref([])
const logs = ref([])
const loading = ref(false)

const columns = [
  { title: '时间', dataIndex: 'timestamp', key: 'timestamp' },
  { title: '用户', dataIndex: 'username', key: 'username' },
  { title: '应用', dataIndex: 'clientName', key: 'clientName' },
  { title: 'Scope', dataIndex: 'scope', key: 'scope' },
  { title: '资源路径', dataIndex: 'resourcePath', key: 'resourcePath' },
  { title: '方法', dataIndex: 'httpMethod', key: 'httpMethod' },
  { title: '状态', dataIndex: 'httpStatus', key: 'httpStatus' },
  { title: '是否异常', key: 'isAnomaly' }
]

const resetQuery = () => {
  userId.value = ''
  clientId.value = ''
  dateRange.value = []
  logs.value = []
}

const fetchLogs = async () => {
  loading.value = true
  try {
    let url = ''
    
    if (queryType.value === 'user' && userId.value) {
      url = `/api/audit/logs/user/${userId.value}`
    } else if (queryType.value === 'client' && clientId.value) {
      url = `/api/audit/logs/client/${clientId.value}`
    } else if (queryType.value === 'anomalies') {
      url = '/api/audit/logs/anomalies'
    } else if (queryType.value === 'range' && dateRange.value.length === 2) {
      const [start, end] = dateRange.value
      url = `/api/audit/logs/range?start=${start.toISOString()}&end=${end.toISOString()}`
    }
    
    if (url) {
      const response = await axios.get(url)
      logs.value = response.data
      message.success(`查询到 ${logs.value.length} 条记录`)
    }
  } catch (error) {
    console.error('Failed to fetch logs:', error)
    message.error('获取日志失败')
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  const now = new Date()
  const yesterday = new Date(now.getTime() - 24 * 60 * 60 * 1000)
  dateRange.value = [yesterday, now]
  fetchLogs()
})
</script>
