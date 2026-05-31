<template>
  <div class="notification-center">
    <el-popover
      ref="popoverRef"
      placement="bottom-end"
      :width="360"
      trigger="click"
      popper-class="notification-popover"
    >
      <template #reference>
        <el-badge :value="unreadCount" :hidden="unreadCount === 0" :max="99" class="notification-badge">
          <el-button :icon="Bell" circle />
        </el-badge>
      </template>
      <div class="notification-panel">
        <div class="panel-header">
          <span class="panel-title">消息通知</span>
          <el-button size="small" text @click="clearAll" v-if="notifications.length">全部已读</el-button>
        </div>
        <div v-if="!notifications.length" class="empty-state">
          <el-empty description="暂无新消息" :image-size="60" />
        </div>
        <div v-else class="notification-list">
          <div
            v-for="item in notifications"
            :key="item.id"
            class="notification-item"
            :class="{ unread: !item.read }"
            @click="handleClick(item)"
          >
            <el-avatar :size="32" :icon="iconForType(item.type)" :class="avatarClass(item.type)" />
            <div class="notification-body">
              <div class="notification-title">
                {{ item.title }}
                <el-tag v-if="!item.read" size="small" type="danger" effect="plain" style="margin-left:4px">新</el-tag>
              </div>
              <div class="notification-content">{{ item.content }}</div>
              <div class="notification-time">{{ formatTime(item.timestamp) }}</div>
            </div>
          </div>
        </div>
      </div>
    </el-popover>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, markRaw } from 'vue'
import { Bell, Document, Refresh, Warning, InfoFilled } from '@element-plus/icons-vue'

interface Notification {
  id: string
  type: string
  title: string
  content: string
  timestamp: number
  read: boolean
  data?: any
}

const notifications = ref<Notification[]>([])
const popoverRef = ref()
const maxStored = 50
const storageKey = 'api_docs_notifications'

const unreadCount = computed(() => notifications.value.filter((n) => !n.read).length)

function loadStored() {
  try {
    const s = localStorage.getItem(storageKey)
    if (s) notifications.value = JSON.parse(s)
  } catch (e) {}
}

function save() {
  try {
    localStorage.setItem(storageKey, JSON.stringify(notifications.value.slice(0, maxStored)))
  } catch (e) {}
}

function addNotification(payload: any) {
  const item: Notification = {
    id: `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`,
    type: payload.type || 'info',
    title: payload.title || '通知',
    content: payload.content || '',
    timestamp: Date.now(),
    read: false,
    data: payload.data
  }
  notifications.value.unshift(item)
  if (notifications.value.length > maxStored) notifications.value.pop()
  save()
}

function handleClick(item: Notification) {
  item.read = true
  save()
  if (item.data?.docId) {
    // 可扩展为跳转到对应文档
  }
}

function clearAll() {
  notifications.value.forEach((n) => (n.read = true))
  save()
}

function formatTime(ts: number): string {
  const diff = (Date.now() - ts) / 1000
  if (diff < 60) return '刚刚'
  if (diff < 3600) return `${Math.floor(diff / 60)} 分钟前`
  if (diff < 86400) return `${Math.floor(diff / 3600)} 小时前`
  return new Date(ts).toLocaleDateString()
}

function iconForType(type: string) {
  switch (type) {
    case 'doc_created':
    case 'doc_updated':
    case 'doc_deleted':
      return markRaw(Document)
    case 'version_rollback':
      return markRaw(Refresh)
    default:
      return markRaw(InfoFilled)
  }
}

function avatarClass(type: string) {
  switch (type) {
    case 'doc_created': return 'avatar-success'
    case 'doc_updated': return 'avatar-primary'
    case 'doc_deleted': return 'avatar-danger'
    default: return 'avatar-info'
  }
}

// WebSocket 连接
let socket: WebSocket | null = null
let reconnectTimer: number | null = null
let reconnectAttempts = 0
const maxReconnectDelay = 30000

function getWsUrl(): string {
  const base = (import.meta as any).env?.VITE_API_BASE || window.location.origin
  const wsProto = base.startsWith('https') ? 'wss' : 'ws'
  const host = base.replace(/^https?:\/\//, '').replace(/\/$/, '')
  const token = localStorage.getItem('token') || ''
  return `${wsProto}://${host}/notifications?token=${encodeURIComponent(token)}`
}

function connectWs() {
  try {
    socket = new WebSocket(getWsUrl())
    socket.onopen = () => {
      reconnectAttempts = 0
    }
    socket.onmessage = (ev) => {
      try {
        const data = JSON.parse(ev.data)
        if (data.type === 'notification') {
          addNotification(data.payload || data)
        } else if (data.event === 'notification') {
          addNotification(data)
        }
      } catch (e) {}
    }
    socket.onclose = () => {
      scheduleReconnect()
    }
    socket.onerror = () => {
      socket?.close()
    }
  } catch (e) {
    scheduleReconnect()
  }
}

function scheduleReconnect() {
  if (reconnectTimer) return
  reconnectAttempts++
  const delay = Math.min(1000 * Math.pow(2, reconnectAttempts), maxReconnectDelay)
  reconnectTimer = window.setTimeout(() => {
    reconnectTimer = null
    connectWs()
  }, delay)
}

onMounted(() => {
  loadStored()
  connectWs()
})

onUnmounted(() => {
  if (reconnectTimer) clearTimeout(reconnectTimer)
  socket?.close()
})
</script>

<style scoped>
.notification-badge {
  cursor: pointer;
}
.notification-panel {
  max-height: 420px;
  display: flex;
  flex-direction: column;
}
.panel-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 16px;
  border-bottom: 1px solid #ebeef5;
}
.panel-title {
  font-weight: 600;
  font-size: 14px;
}
.notification-list {
  flex: 1;
  overflow-y: auto;
  max-height: 360px;
}
.notification-item {
  display: flex;
  gap: 12px;
  padding: 12px 16px;
  cursor: pointer;
  border-bottom: 1px solid #f5f5f5;
  transition: background 0.15s;
}
.notification-item:hover {
  background: #f5f7fa;
}
.notification-item.unread {
  background: #ecf5ff;
}
.notification-body {
  flex: 1;
  min-width: 0;
}
.notification-title {
  font-size: 13px;
  font-weight: 500;
  color: #303133;
  margin-bottom: 2px;
}
.notification-content {
  font-size: 12px;
  color: #606266;
  overflow: hidden;
  text-overflow: ellipsis;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
}
.notification-time {
  font-size: 11px;
  color: #909399;
  margin-top: 4px;
}
.empty-state {
  padding: 24px 0;
}
:deep(.el-avatar.avatar-success) { background: #67c23a; color: #fff; }
:deep(.el-avatar.avatar-primary) { background: #409eff; color: #fff; }
:deep(.el-avatar.avatar-danger) { background: #f56c6c; color: #fff; }
:deep(.el-avatar.avatar-info) { background: #909399; color: #fff; }
</style>
