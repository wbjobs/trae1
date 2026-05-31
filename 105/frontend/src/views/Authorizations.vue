<template>
  <div>
    <a-page-header title="授权管理" sub-title="管理用户的 OAuth2 授权">
      <template #extra>
        <a-button type="primary" @click="showCreateModal = true">
          <template #icon><plus-outlined /></template>
          新建授权
        </a-button>
      </template>
    </a-page-header>

    <a-card style="margin-top: 16px">
      <a-form layout="inline" style="margin-bottom: 16px">
        <a-form-item label="选择用户">
          <a-select v-model:value="selectedUserId" placeholder="选择用户" style="width: 200px" @change="fetchAuthorizations">
            <a-select-option :value="1">admin</a-select-option>
            <a-select-option :value="2">user1</a-select-option>
            <a-select-option :value="3">user2</a-select-option>
          </a-select>
        </a-form-item>
        <a-form-item>
          <a-popconfirm
            title="确定要登出该用户的所有设备吗？此操作将撤销所有授权"
            @confirm="logoutAllDevices"
          >
            <a-button danger :loading="loggingOut">
              <template #icon><logout-outlined /></template>
              一键登出所有设备
            </a-button>
          </a-popconfirm>
        </a-form-item>
      </a-form>

      <a-tabs v-model:activeKey="activeTab">
        <a-tab-pane key="active" tab="当前授权">
          <a-table :columns="columns" :data-source="activeAuthorizations" row-key="id" :loading="loading">
            <template #bodyCell="{ column, record }">
              <template v-if="column.key === 'scopes'">
                <a-tag v-for="scope in record.scopes" :key="scope" style="margin-right: 4px">{{ scope }}</a-tag>
              </template>
              <template v-if="column.key === 'active'">
                <a-badge status="success" text="有效" />
              </template>
              <template v-if="column.key === 'device'">
                <a-tooltip :title="`IP: ${record.ipAddress || 'N/A'}\n指纹: ${record.deviceFingerprint || 'N/A'}`">
                  <desktop-outlined style="font-size: 16px; color: #1890ff" />
                </a-tooltip>
                <span style="margin-left: 8px">{{ record.deviceName || 'Unknown Device' }}</span>
              </template>
              <template v-if="column.key === 'action'">
                <a-popconfirm
                  title="确定要撤销这个授权吗？"
                  @confirm="revokeAuthorization(record.id)"
                >
                  <a-button type="link" danger size="small">撤销授权</a-button>
                </a-popconfirm>
              </template>
            </template>
          </a-table>
        </a-tab-pane>

        <a-tab-pane key="history" tab="历史记录">
          <a-table :columns="historyColumns" :data-source="authorizations" row-key="id" :loading="loading">
            <template #bodyCell="{ column, record }">
              <template v-if="column.key === 'scopes'">
                <a-tag v-for="scope in record.scopes" :key="scope" style="margin-right: 4px">{{ scope }}</a-tag>
              </template>
              <template v-if="column.key === 'active'">
                <a-badge :status="record.active ? 'success' : 'error'" :text="record.active ? '有效' : '已撤销'" />
              </template>
              <template v-if="column.key === 'device'">
                <a-descriptions size="small" :column="1">
                  <a-descriptions-item label="设备">
                    <desktop-outlined style="color: #1890ff" />
                    {{ record.deviceName || 'Unknown' }}
                  </a-descriptions-item>
                  <a-descriptions-item label="IP">{{ record.ipAddress || 'N/A' }}</a-descriptions-item>
                  <a-descriptions-item label="指纹" style="font-size: 10px; word-break: break-all;">
                    {{ record.deviceFingerprint || 'N/A' }}
                  </a-descriptions-item>
                </a-descriptions>
              </template>
            </template>
          </a-table>
        </a-tab-pane>
      </a-tabs>
    </a-card>

    <a-modal
      v-model:open="showCreateModal"
      title="新建授权"
      @ok="createAuthorization"
      @cancel="showCreateModal = false"
      width="700px"
    >
      <a-form layout="vertical">
        <a-form-item label="用户">
          <a-select v-model:value="newAuthorization.userId" style="width: 100%">
            <a-select-option :value="1">admin</a-select-option>
            <a-select-option :value="2">user1</a-select-option>
            <a-select-option :value="3">user2</a-select-option>
          </a-select>
        </a-form-item>
        <a-form-item label="应用">
          <a-select v-model:value="newAuthorization.clientId" style="width: 100%">
            <a-select-option :value="1">demo-client</a-select-option>
            <a-select-option :value="2">analytics-app</a-select-option>
          </a-select>
        </a-form-item>
        <a-form-item label="授权范围">
          <a-select
            v-model:value="newAuthorization.scopes"
            mode="multiple"
            placeholder="请选择授权范围"
            style="width: 100%"
          >
            <a-optgroup label="基本信息权限">
              <a-select-option value="profile">profile - 读取你的昵称和头像</a-select-option>
              <a-select-option value="name">name - 读取你的真实姓名</a-select-option>
              <a-select-option value="picture">picture - 读取你的头像图片</a-select-option>
            </a-optgroup>
            <a-optgroup label="读写权限">
              <a-select-option value="user:read">user:read - 读取用户信息</a-select-option>
              <a-select-option value="user:write">user:write - 修改用户信息</a-select-option>
              <a-select-option value="post:read">post:read - 读取帖子</a-select-option>
              <a-select-option value="post:write">post:write - 发帖</a-select-option>
            </a-optgroup>
            <a-optgroup label="敏感信息权限">
              <a-select-option value="email">email - 读取你的邮箱地址</a-select-option>
              <a-select-option value="phone">phone - 读取你的手机号码</a-select-option>
              <a-select-option value="address">address - 读取你的收货地址</a-select-option>
            </a-optgroup>
            <a-optgroup label="高危权限">
              <a-select-option value="admin:all">
                <span style="color: #f5222d">admin:all - 管理员权限（高危）</span>
              </a-select-option>
              <a-select-option value="finance:read">
                <span style="color: #f5222d">finance:read - 财务读取（高危）</span>
              </a-select-option>
              <a-select-option value="payment:write">
                <span style="color: #f5222d">payment:write - 支付权限（高危）</span>
              </a-select-option>
            </a-optgroup>
          </a-select>
        </a-form-item>
        <a-form-item label="有效期">
          <a-select v-model:value="newAuthorization.duration" style="width: 100%">
            <a-select-option value="ONE_HOUR">
              <span>1小时 <a-tag color="green">临时授权</a-tag></span>
            </a-select-option>
            <a-select-option value="SEVEN_DAYS">
              <span>7天 <a-tag color="blue">短期授权</a-tag></span>
            </a-select-option>
            <a-select-option value="PERMANENT">
              <span>永久 <a-tag color="orange">长期授权</a-tag></span>
            </a-select-option>
          </a-select>
        </a-form-item>
        <a-divider>设备信息</a-divider>
        <a-form-item label="设备指纹">
          <a-input v-model:value="newAuthorization.deviceFingerprint" readonly />
        </a-form-item>
        <a-row :gutter="16">
          <a-col :span="12">
            <a-form-item label="设备名称">
              <a-input v-model:value="newAuthorization.deviceName" />
            </a-form-item>
          </a-col>
          <a-col :span="12">
            <a-form-item label="IP地址">
              <a-input v-model:value="newAuthorization.ipAddress" />
            </a-form-item>
          </a-col>
        </a-row>
      </a-form>
    </a-modal>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import axios from 'axios'
import { PlusOutlined, LogoutOutlined, DesktopOutlined } from '@ant-design/icons-vue'
import { message, Modal } from 'ant-design-vue'

const selectedUserId = ref(1)
const authorizations = ref([])
const loading = ref(false)
const loggingOut = ref(false)
const showCreateModal = ref(false)
const activeTab = ref('active')

const newAuthorization = ref({
  userId: 1,
  clientId: 1,
  scopes: [],
  duration: 'SEVEN_DAYS',
  deviceFingerprint: '',
  deviceName: '',
  ipAddress: '',
  userAgent: ''
})

const columns = [
  { title: 'ID', dataIndex: 'id', key: 'id', width: 60 },
  { title: '应用', dataIndex: ['clientApplication', 'clientName'], key: 'client' },
  { title: '授权范围', key: 'scopes', width: 300 },
  { title: '有效期', dataIndex: 'duration', key: 'duration' },
  { title: '过期时间', dataIndex: 'expiresAt', key: 'expiresAt' },
  { title: '设备信息', key: 'device' },
  { title: '状态', key: 'active', width: 80 },
  { title: '操作', key: 'action', width: 100 }
]

const historyColumns = [
  { title: 'ID', dataIndex: 'id', key: 'id', width: 60 },
  { title: '应用', dataIndex: ['clientApplication', 'clientName'], key: 'client' },
  { title: '授权范围', key: 'scopes', width: 200 },
  { title: '有效期', dataIndex: 'duration', key: 'duration' },
  { title: '授权时间', dataIndex: 'authorizedAt', key: 'authorizedAt' },
  { title: '撤销时间', dataIndex: 'revokedAt', key: 'revokedAt' },
  { title: '设备信息', key: 'device', width: 200 },
  { title: '状态', key: 'active', width: 80 }
]

const activeAuthorizations = computed(() => {
  return authorizations.value.filter(auth => auth.active)
})

const fetchAuthorizations = async () => {
  loading.value = true
  try {
    const response = await axios.get(`/api/authorizations/user/${selectedUserId.value}/history`)
    authorizations.value = response.data
  } catch (error) {
    console.error('Failed to fetch authorizations:', error)
    message.error('获取授权列表失败')
  } finally {
    loading.value = false
  }
}

const createAuthorization = async () => {
  try {
    await axios.post('/api/authorizations', newAuthorization.value)
    message.success('授权创建成功')
    showCreateModal.value = false
    fetchAuthorizations()
  } catch (error) {
    console.error('Failed to create authorization:', error)
    message.error('创建授权失败')
  }
}

const revokeAuthorization = async (id) => {
  try {
    await axios.delete(`/api/authorizations/${id}`)
    message.success('授权已撤销')
    fetchAuthorizations()
  } catch (error) {
    console.error('Failed to revoke authorization:', error)
    message.error('撤销授权失败')
  }
}

const logoutAllDevices = async () => {
  loggingOut.value = true
  try {
    await axios.delete(`/api/authorizations/user/${selectedUserId.value}/all`)
    message.success('已登出所有设备')
    fetchAuthorizations()
  } catch (error) {
    console.error('Failed to logout all devices:', error)
    message.error('操作失败')
  } finally {
    loggingOut.value = false
  }
}

const generateDeviceFingerprint = () => {
  const canvas = document.createElement('canvas')
  const ctx = canvas.getContext('2d')
  ctx.textBaseline = 'top'
  ctx.font = '14px Arial'
  ctx.fillStyle = '#f60'
  ctx.fillRect(125, 1, 62, 20)
  ctx.fillStyle = '#069'
  ctx.fillText('OAuth2 Audit', 2, 15)
  ctx.fillStyle = 'rgba(102, 204, 0, 0.7)'
  ctx.fillText('Device FP', 4, 17)

  const fingerprint = [
    navigator.userAgent,
    navigator.language,
    screen.width + 'x' + screen.height,
    new Date().getTimezoneOffset(),
    canvas.toDataURL()
  ].join('|')

  let hash = 0
  for (let i = 0; i < fingerprint.length; i++) {
    const char = fingerprint.charCodeAt(i)
    hash = ((hash << 5) - hash) + char
    hash = hash & hash
  }

  return Math.abs(hash).toString(16).toUpperCase()
}

const parseUserAgent = () => {
  const ua = navigator.userAgent
  if (ua.includes('Windows')) return 'Windows PC'
  if (ua.includes('Mac OS')) return 'Mac'
  if (ua.includes('Linux')) return 'Linux PC'
  if (ua.includes('iPhone')) return 'iPhone'
  if (ua.includes('iPad')) return 'iPad'
  if (ua.includes('Android')) return 'Android Device'
  return 'Unknown Device'
}

const initNewAuthorizationDeviceInfo = async () => {
  newAuthorization.value.deviceFingerprint = generateDeviceFingerprint()
  newAuthorization.value.deviceName = parseUserAgent()
  newAuthorization.value.userAgent = navigator.userAgent

  try {
    const response = await axios.get('https://api.ipify.org?format=json')
    newAuthorization.value.ipAddress = response.data.ip
  } catch {
    newAuthorization.value.ipAddress = '127.0.0.1'
  }
}

onMounted(() => {
  fetchAuthorizations()
  initNewAuthorizationDeviceInfo()
})
</script>
