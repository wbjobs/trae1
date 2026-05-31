<template>
  <div class="authorization-page">
    <a-card title="授权确认" style="max-width: 800px; margin: 0 auto">
      <template #extra>
        <a-tag color="blue">{{ clientName }}</a-tag>
      </template>

      <a-alert
        message="即将授权给第三方应用"
        :description="`${clientName} 请求以下权限，请仔细确认后再授权`"
        type="info"
        show-icon
        style="margin-bottom: 24px"
      />

      <div v-for="(scopes, group) in groupedScopes" :key="group" class="scope-group">
        <a-divider orientation="left">
          <span :class="['group-title', getGroupClass(group)]">
            {{ getGroupTitle(group) }}
          </span>
          <a-tag :color="getGroupColor(group)" style="margin-left: 8px">
            {{ scopes.length }} 项权限
          </a-tag>
        </a-divider>

        <a-list size="small" :data-source="scopes">
          <template #renderItem="{ item }">
            <a-list-item>
              <a-list-item-meta>
                <template #title>
                  <span :class="{ 'high-risk': item.highRisk }">
                    {{ item.displayName }}
                    <a-tag v-if="item.highRisk" color="red" style="margin-left: 8px">高危</a-tag>
                  </span>
                </template>
                <template #description>
                  <span class="scope-description">{{ item.description }}</span>
                </template>
              </a-list-item-meta>
              <template #actions>
                <a-tag color="processing">{{ item.scope }}</a-tag>
              </template>
            </a-list-item>
          </template>
        </a-list>
      </div>

      <a-divider />

      <a-form layout="vertical">
        <a-form-item label="授权有效期">
          <a-select v-model:value="selectedDuration" style="width: 100%">
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

        <a-form-item label="当前设备信息">
          <aDescriptions size="small" :column="2">
            <aDescriptions-item label="设备指纹">{{ deviceInfo.fingerprint }}</aDescriptions-item>
            <aDescriptions-item label="设备名称">{{ deviceInfo.name }}</aDescriptions-item>
            <aDescriptions-item label="IP地址">{{ deviceInfo.ip }}</aDescriptions-item>
            <aDescriptions-item label="User Agent">{{ truncateUA(deviceInfo.userAgent) }}</aDescriptions-item>
          </aDescriptions>
        </a-form-item>

        <a-form-item v-if="hasHighRiskScopes">
          <a-alert
            message="高危权限授权"
            description="您即将授权高危权限，需要进行二次确认。请输入您的支付密码以继续"
            type="warning"
            show-icon
            style="margin-bottom: 16px"
          />
          <a-input-password v-model:value="confirmPassword" placeholder="请输入支付密码进行确认" />
        </a-form-item>

        <a-form-item>
          <a-space style="width: 100%; justify-content: space-between">
            <a-button @click="handleCancel">取消</a-button>
            <a-button type="primary" :loading="loading" @click="handleAuthorize" :disabled="!canAuthorize">
              确认授权
            </a-button>
          </a-space>
        </a-form-item>
      </a-form>
    </a-card>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { message } from 'ant-design-vue'
import axios from 'axios'

const props = defineProps({
  clientId: { type: String, required: true },
  scopes: { type: Array, required: true },
  clientName: { type: String, default: '第三方应用' },
  redirectUri: { type: String, default: '' }
})

const emit = defineEmits(['authorize', 'cancel'])

const selectedDuration = ref('SEVEN_DAYS')
const confirmPassword = ref('')
const loading = ref(false)

const deviceInfo = ref({
  fingerprint: '',
  name: '',
  ip: '',
  userAgent: ''
})

const scopeConfig = {
  basic: {
    title: '基本信息权限',
    color: 'green',
    class: 'basic-group',
    scopes: {
      'profile': { displayName: '基本信息', description: '读取你的昵称和头像', highRisk: false },
      'name': { displayName: '姓名', description: '读取你的真实姓名', highRisk: false },
      'picture': { displayName: '头像', description: '读取你的头像图片', highRisk: false },
      'gender': { displayName: '性别', description: '读取你的性别信息', highRisk: false }
    }
  },
  read_write: {
    title: '读写权限',
    color: 'blue',
    class: 'read-write-group',
    scopes: {
      'user:read': { displayName: '读取用户信息', description: '读取你的用户资料信息', highRisk: false },
      'user:write': { displayName: '修改用户信息', description: '修改你的个人资料', highRisk: false },
      'post:read': { displayName: '读取帖子', description: '读取你发布的帖子内容', highRisk: false },
      'post:write': { displayName: '发帖', description: '以你的名义发布新帖子', highRisk: false },
      'file:read': { displayName: '读取文件', description: '访问你的云文件', highRisk: false },
      'file:write': { displayName: '写入文件', description: '上传和管理你的云文件', highRisk: false }
    }
  },
  sensitive: {
    title: '敏感信息权限',
    color: 'orange',
    class: 'sensitive-group',
    scopes: {
      'email': { displayName: '邮箱', description: '读取你的邮箱地址', highRisk: false },
      'phone': { displayName: '手机号', description: '读取你的手机号码', highRisk: false },
      'address': { displayName: '地址', description: '读取你的收货地址', highRisk: false }
    }
  },
  high_risk: {
    title: '高危权限',
    color: 'red',
    class: 'high-risk-group',
    scopes: {
      'admin:all': { displayName: '管理员权限', description: '拥有所有管理员权限，可以执行任何操作', highRisk: true },
      'finance:read': { displayName: '财务读取', description: '查看你的账户余额和交易记录', highRisk: true },
      'payment:write': { displayName: '支付权限', description: '发起支付交易', highRisk: true },
      'data:export': { displayName: '数据导出', description: '导出你的所有个人数据', highRisk: true }
    }
  }
}

const groupedScopes = computed(() => {
  const groups = {
    basic: [],
    read_write: [],
    sensitive: [],
    high_risk: []
  }

  props.scopes.forEach(scope => {
    for (const [groupKey, group] of Object.entries(scopeConfig)) {
      if (group.scopes[scope]) {
        groups[groupKey].push({
          scope,
          ...group.scopes[scope]
        })
        break
      }
    }

    let found = false
    for (const group of Object.values(scopeConfig)) {
      if (group.scopes[scope]) {
        found = true
        break
      }
    }
    if (!found) {
      groups.basic.push({
        scope,
        displayName: scope,
        description: `权限: ${scope}`,
        highRisk: false
      })
    }
  })

  Object.keys(groups).forEach(key => {
    if (groups[key].length === 0) {
      delete groups[key]
    }
  })

  return groups
})

const hasHighRiskScopes = computed(() => {
  return Object.values(groupedScopes.value)
    .flat()
    .some(scope => scope.highRisk)
})

const canAuthorize = computed(() => {
  if (hasHighRiskScopes.value && !confirmPassword.value) {
    return false
  }
  return true
})

const getGroupTitle = (group) => {
  return scopeConfig[group]?.title || group
}

const getGroupColor = (group) => {
  return scopeConfig[group]?.color || 'default'
}

const getGroupClass = (group) => {
  return scopeConfig[group]?.class || ''
}

const truncateUA = (ua) => {
  if (!ua) return 'Unknown'
  return ua.length > 50 ? ua.substring(0, 50) + '...' : ua
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

const getClientIP = async () => {
  try {
    const response = await axios.get('https://api.ipify.org?format=json')
    return response.data.ip
  } catch {
    return '127.0.0.1'
  }
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

const initDeviceInfo = async () => {
  deviceInfo.value = {
    fingerprint: generateDeviceFingerprint(),
    name: parseUserAgent(),
    ip: await getClientIP(),
    userAgent: navigator.userAgent
  }
}

const handleAuthorize = async () => {
  loading.value = true
  try {
    const userId = 1

    await axios.post('/api/authorizations', {
      userId,
      clientId: parseInt(props.clientId),
      scopes: props.scopes,
      duration: selectedDuration.value,
      ipAddress: deviceInfo.value.ip,
      deviceFingerprint: deviceInfo.value.fingerprint,
      deviceName: deviceInfo.value.name,
      userAgent: deviceInfo.value.userAgent
    })

    message.success('授权成功')
    emit('authorize')
  } catch (error) {
    console.error('Authorization failed:', error)
    message.error('授权失败，请重试')
  } finally {
    loading.value = false
  }
}

const handleCancel = () => {
  emit('cancel')
}

onMounted(() => {
  initDeviceInfo()
})
</script>

<style scoped>
.authorization-page {
  padding: 24px;
  background: #f0f2f5;
  min-height: 100vh;
}

.scope-group {
  margin-bottom: 16px;
}

.group-title {
  font-weight: 600;
  font-size: 16px;
}

.group-title.basic-group {
  color: #52c41a;
}

.group-title.read-write-group {
  color: #1890ff;
}

.group-title.sensitive-group {
  color: #fa8c16;
}

.group-title.high-risk-group {
  color: #f5222d;
}

.high-risk {
  color: #f5222d;
  font-weight: 600;
}

.scope-description {
  color: #8c8c8c;
  font-size: 12px;
}
</style>
