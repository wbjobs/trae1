<template>
  <div>
    <a-page-header title="风险事件" sub-title="基于机器学习的异常授权检测">
      <template #extra>
        <a-button @click="fetchRiskEvents" :loading="loading">
          <template #icon><reload-outlined /></template>
          刷新
        </a-button>
        <a-button @click="triggerModelTraining" :loading="training">
          <template #icon><experiment-outlined /></template>
          重新训练模型
        </a-button>
      </template>
    </a-page-header>

    <a-row :gutter="16" style="margin-top: 16px">
      <a-col :span="6">
        <a-statistic title="待处理事件" :value="statistics.pendingCount || 0" :value-style="{ color: '#faad14' }">
          <template #prefix><warning-outlined /></template>
        </a-statistic>
      </a-col>
      <a-col :span="6">
        <a-statistic title="高危事件" :value="statistics.byLevel?.HIGH || 0" :value-style="{ color: '#f5222d' }">
          <template #prefix><exclamation-circle-outlined /></template>
        </a-statistic>
      </a-col>
      <a-col :span="6">
        <a-statistic title="总事件数" :value="statistics.totalEvents || 0" :value-style="{ color: '#1890ff' }">
          <template #prefix><file-text-outlined /></template>
        </a-statistic>
      </a-col>
      <a-col :span="6">
        <a-card size="small">
          <a-descriptions :column="1" size="small">
            <a-descriptions-item label="模型状态">
              <a-badge :status="modelReady ? 'success' : 'error'" :text="modelReady ? '已就绪' : '未就绪'" />
            </a-descriptions-item>
            <a-descriptions-item label="最后训练">
              {{ formatDate(lastTrainingTime) }}
            </a-descriptions-item>
          </a-descriptions>
        </a-card>
      </a-col>
    </a-row>

    <a-tabs v-model:activeKey="activeTab" style="margin-top: 16px">
      <a-tab-pane key="pending" tab="待处理">
        <a-table :columns="columns" :data-source="pendingEvents" row-key="id" :loading="loading" :pagination="{ pageSize: 10 }">
          <template #bodyCell="{ column, record }">
            <template v-if="column.key === 'riskLevel'">
              <a-tag :color="getRiskLevelColor(record.riskLevel)">
                {{ record.riskLevel }}
              </a-tag>
            </template>
            <template v-if="column.key === 'riskType'">
              {{ record.riskType }}
            </template>
            <template v-if="column.key === 'anomalyScore'">
              <a-progress
                :percent="Math.round(record.anomalyScore * 100)"
                :status="record.anomalyScore > 0.7 ? 'exception' : 'active'"
                size="small"
                style="width: 100px"
              />
            </template>
            <template v-if="column.key === 'status'">
              <a-badge :status="getStatusBadge(record.status)" :text="getStatusText(record.status)" />
            </template>
            <template v-if="column.key === 'action'">
              <a-space>
                <a-button type="link" size="small" @click="showDetail(record)">详情</a-button>
                <a-popconfirm
                  title="确认这是您本人的操作？"
                  @confirm="confirmAsSelf(record.id)"
                >
                  <a-button type="link" size="small" style="color: #52c41a">本人操作</a-button>
                </a-popconfirm>
                <a-popconfirm
                  title="确定要撤销此授权吗？"
                  @confirm="revokeAuthorization(record.id)"
                >
                  <a-button type="link" danger size="small">立即撤销</a-button>
                </a-popconfirm>
              </a-space>
            </template>
          </template>
        </a-table>
      </a-tab-pane>

      <a-tab-pane key="all" tab="全部事件">
        <a-table :columns="columns" :data-source="allEvents" row-key="id" :loading="loading" :pagination="{ pageSize: 10 }">
          <template #bodyCell="{ column, record }">
            <template v-if="column.key === 'riskLevel'">
              <a-tag :color="getRiskLevelColor(record.riskLevel)">
                {{ record.riskLevel }}
              </a-tag>
            </template>
            <template v-if="column.key === 'riskType'">
              {{ record.riskType }}
            </template>
            <template v-if="column.key === 'anomalyScore'">
              <a-progress
                :percent="Math.round(record.anomalyScore * 100)"
                :status="record.anomalyScore > 0.7 ? 'exception' : 'active'"
                size="small"
                style="width: 100px"
              />
            </template>
            <template v-if="column.key === 'status'">
              <a-badge :status="getStatusBadge(record.status)" :text="getStatusText(record.status)" />
            </template>
            <template v-if="column.key === 'action'">
              <a-button type="link" size="small" @click="showDetail(record)">详情</a-button>
            </template>
          </template>
        </a-table>
      </a-tab-pane>
    </a-tabs>

    <a-modal
      v-model:open="detailModalVisible"
      title="风险事件详情"
      width="600px"
      :footer="null"
    >
      <a-descriptions :column="2" bordered v-if="selectedEvent">
        <a-descriptions-item label="事件ID">{{ selectedEvent.id }}</a-descriptions-item>
        <a-descriptions-item label="用户">{{ selectedEvent.user?.username || 'N/A' }}</a-descriptions-item>
        <a-descriptions-item label="风险等级">
          <a-tag :color="getRiskLevelColor(selectedEvent.riskLevel)">{{ selectedEvent.riskLevel }}</a-tag>
        </a-descriptions-item>
        <a-descriptions-item label="风险类型">{{ selectedEvent.riskType }}</a-descriptions-item>
        <a-descriptions-item label="异常评分">
          <a-progress
            :percent="Math.round(selectedEvent.anomalyScore * 100)"
            :status="selectedEvent.anomalyScore > 0.7 ? 'exception' : 'active'"
            size="small"
          />
        </a-descriptions-item>
        <a-descriptions-item label="状态">
          <a-badge :status="getStatusBadge(selectedEvent.status)" :text="getStatusText(selectedEvent.status)" />
        </a-descriptions-item>
        <a-descriptions-item label="检测时间">{{ selectedEvent.detectedAt }}</a-descriptions-item>
        <a-descriptions-item label="处理时间">
          {{ selectedEvent.confirmedAt || selectedEvent.resolvedAt || 'N/A' }}
        </a-descriptions-item>
        <a-descriptions-item label="关联应用" :span="2">
          {{ selectedEvent.authorization?.clientApplication?.clientName || 'N/A' }}
        </a-descriptions-item>
        <a-descriptions-item label="IP地址" :span="2">
          {{ selectedEvent.authorization?.ipAddress || 'N/A' }}
        </a-descriptions-item>
        <a-descriptions-item label="设备" :span="2">
          {{ selectedEvent.authorization?.deviceName || 'N/A' }}
        </a-descriptions-item>
        <a-descriptions-item label="风险原因" :span="2">
          {{ selectedEvent.riskReason }}
        </a-descriptions-item>
      </a-descriptions>
    </a-modal>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import axios from 'axios'
import { message } from 'ant-design-vue'
import { ReloadOutlined, ExperimentOutlined, WarningOutlined, ExclamationCircleOutlined, FileTextOutlined } from '@ant-design/icons-vue'

const loading = ref(false)
const training = ref(false)
const activeTab = ref('pending')
const allEvents = ref([])
const pendingEvents = ref([])
const statistics = ref({})
const modelReady = ref(false)
const lastTrainingTime = ref(0)
const detailModalVisible = ref(false)
const selectedEvent = ref(null)

const columns = [
  { title: 'ID', dataIndex: 'id', key: 'id', width: 60 },
  { title: '用户', dataIndex: ['user', 'username'], key: 'user', width: 100 },
  { title: '风险等级', key: 'riskLevel', width: 100 },
  { title: '风险类型', key: 'riskType', width: 150 },
  { title: '异常评分', key: 'anomalyScore', width: 120 },
  { title: '状态', key: 'status', width: 100 },
  { title: '检测时间', dataIndex: 'detectedAt', key: 'detectedAt', width: 180 },
  { title: '操作', key: 'action', width: 200 }
]

const fetchRiskEvents = async () => {
  loading.value = true
  try {
    const response = await axios.get('/api/risk-events')
    allEvents.value = response.data

    const pendingResponse = await axios.get('/api/risk-events/pending')
    pendingEvents.value = pendingResponse.data

    const statsResponse = await axios.get('/api/risk-events/statistics')
    statistics.value = statsResponse.data

    const modelResponse = await axios.get('/api/risk-events/model/status')
    modelReady.value = modelResponse.data.ready
    lastTrainingTime.value = modelResponse.data.lastTrainingTime
  } catch (error) {
    console.error('Failed to fetch risk events:', error)
    message.error('获取风险事件失败')
  } finally {
    loading.value = false
  }
}

const triggerModelTraining = async () => {
  training.value = true
  try {
    await axios.post('/api/risk-events/model/train')
    message.success('模型训练已触发')
    setTimeout(() => fetchRiskEvents(), 2000)
  } catch (error) {
    console.error('Failed to trigger model training:', error)
    message.error('触发训练失败')
  } finally {
    training.value = false
  }
}

const confirmAsSelf = async (eventId) => {
  try {
    await axios.post(`/api/risk-events/${eventId}/confirm-self`)
    message.success('已确认为本人操作')
    fetchRiskEvents()
  } catch (error) {
    console.error('Failed to confirm as self:', error)
    message.error('操作失败')
  }
}

const revokeAuthorization = async (eventId) => {
  try {
    await axios.post(`/api/risk-events/${eventId}/revoke`)
    message.success('授权已撤销')
    fetchRiskEvents()
  } catch (error) {
    console.error('Failed to revoke authorization:', error)
    message.error('操作失败')
  }
}

const showDetail = (event) => {
  selectedEvent.value = event
  detailModalVisible.value = true
}

const getRiskLevelColor = (level) => {
  const colors = {
    'LOW': 'green',
    'MEDIUM': 'orange',
    'HIGH': 'red',
    'CRITICAL': 'red'
  }
  return colors[level] || 'default'
}

const getStatusBadge = (status) => {
  const badges = {
    'PENDING': 'warning',
    'CONFIRMED_SELF': 'success',
    'REVOKED': 'error',
    'DISMISSED': 'default'
  }
  return badges[status] || 'default'
}

const getStatusText = (status) => {
  const texts = {
    'PENDING': '待确认',
    'CONFIRMED_SELF': '本人操作',
    'REVOKED': '已撤销',
    'DISMISSED': '已忽略'
  }
  return texts[status] || status
}

const formatDate = (timestamp) => {
  if (!timestamp) return '从未训练'
  return new Date(timestamp).toLocaleString()
}

onMounted(() => {
  fetchRiskEvents()
})
</script>
