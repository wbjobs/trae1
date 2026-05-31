<template>
  <div class="realtime-sync">
    <div class="sync-status" :class="statusClass">
      <el-icon class="status-icon">
        <component :is="statusIcon" />
      </el-icon>
      <span class="status-text">{{ statusText }}</span>
    </div>

    <div v-if="onlineUsers.length > 0" class="online-users">
      <div class="online-header">
        <span>在线用户 ({{ onlineUsers.length }})</span>
      </div>
      <div class="user-avatars">
        <el-tooltip
          v-for="user in onlineUsers"
          :key="user.id"
          :content="user.username"
          placement="bottom"
        >
          <el-avatar
            :size="32"
            :class="getUserRoleClass(user.role)"
          >
            {{ user.username?.charAt(0).toUpperCase() }}
          </el-avatar>
        </el-tooltip>
      </div>
    </div>

    <div v-if="showActivityLog" class="activity-log">
      <div class="log-header">
        <span>实时动态</span>
        <el-button text size="small" @click="showActivityLog = false">
          <el-icon><Close /></el-icon>
        </el-button>
      </div>
      <div class="log-list">
        <div
          v-for="log in activityLogs"
          :key="log.id"
          class="log-item"
        >
          <el-avatar :size="24" class="log-avatar">
            {{ log.username?.charAt(0).toUpperCase() }}
          </el-avatar>
          <div class="log-content">
            <span class="log-user">{{ log.username }}</span>
            <span class="log-action">{{ log.action }}</span>
            <span class="log-time">{{ formatTime(log.time) }}</span>
          </div>
        </div>
        <el-empty v-if="activityLogs.length === 0" description="暂无动态" :image-size="60" />
      </div>
    </div>

    <div class="sync-actions">
      <el-button
        text
        size="small"
        @click="showActivityLog = !showActivityLog"
      >
        <el-icon><Bell /></el-icon>
        <span>动态</span>
      </el-button>
      <el-button
        text
        size="small"
        :loading="isSyncing"
        @click="handleSync"
      >
        <el-icon><Refresh /></el-icon>
        <span>同步</span>
      </el-button>
    </div>

    <div
      v-if="cursorPosition"
      class="remote-cursor"
      :style="cursorStyle"
    >
      <div class="cursor-line" :style="{ backgroundColor: cursorColor }" />
      <div class="cursor-label" :style="{ backgroundColor: cursorColor }">
        {{ cursorUsername }}
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import socketService from '@/utils/socket'
import { useUserStore } from '@/stores/user'
import dayjs from '@/utils/dayjs'

const props = defineProps({
  documentId: String,
  showActivityLog: {
    type: Boolean,
    default: false
  }
})

const emit = defineEmits(['sync', 'cursor-move'])

const userStore = useUserStore()
const isConnected = ref(false)
const isSyncing = ref(false)
const onlineUsers = ref([])
const activityLogs = ref([])
const cursorPosition = ref(null)
const cursorUsername = ref('')
const cursorColor = ref('#409EFF')

const statusClass = computed(() => ({
  connected: isConnected.value,
  disconnected: !isConnected.value
}))

const statusIcon = computed(() => isConnected.value ? 'Connection' : 'Link')

const statusText = computed(() => isConnected.value ? '已连接' : '未连接')

const cursorStyle = computed(() => {
  if (!cursorPosition.value) return {}
  return {
    left: `${cursorPosition.value.x}px`,
    top: `${cursorPosition.value.y}px`
  }
})

const getUserRoleClass = (role) => {
  return {
    'role-admin': role === 'admin',
    'role-editor': role === 'editor',
    'role-viewer': role === 'viewer'
  }
}

const formatTime = (time) => {
  return dayjs(time).format('HH:mm:ss')
}

const addActivityLog = (log) => {
  activityLogs.value.unshift({
    id: Date.now(),
    ...log,
    time: new Date()
  })
  
  if (activityLogs.value.length > 50) {
    activityLogs.value.pop()
  }
}

const handleConnected = () => {
  isConnected.value = true
}

const handleDisconnected = () => {
  isConnected.value = false
}

const handleUserJoined = (data) => {
  onlineUsers.value = data.onlineUsers
  if (data.user) {
    addActivityLog({
      username: data.user.username,
      action: '加入了文档'
    })
  }
}

const handleUserLeft = (data) => {
  onlineUsers.value = data.onlineUsers
  addActivityLog({
    username: '用户',
    action: '离开了文档'
  })
}

const handleJoinedDocument = (data) => {
  onlineUsers.value = data.onlineUsers
}

const handleAnnotationCreated = (data) => {
  addActivityLog({
    username: data.createdBy?.username || '用户',
    action: '创建了批注'
  })
}

const handleAnnotationUpdated = (data) => {
  addActivityLog({
    username: data.updatedBy?.username || '用户',
    action: '更新了批注'
  })
}

const handleAnnotationDeleted = (data) => {
  addActivityLog({
    username: data.deletedBy?.username || '用户',
    action: '删除了批注'
  })
}

const handleCursorMoved = (data) => {
  cursorPosition.value = data.position
  cursorUsername.value = data.username
  const colors = ['#409EFF', '#67C23A', '#E6A23C', '#F56C6C', '#909399']
  cursorColor.value = colors[Math.abs(data.username?.charCodeAt(0) || 0) % colors.length]
}

const handleSync = async () => {
  isSyncing.value = true
  try {
    emit('sync')
    setTimeout(() => {
      isSyncing.value = false
    }, 1000)
  } catch (error) {
    console.error('Sync error:', error)
    isSyncing.value = false
  }
}

watch(() => props.documentId, (newId) => {
  if (newId) {
    socketService.joinDocument(newId)
  }
})

onMounted(() => {
  socketService.on('connected', handleConnected)
  socketService.on('disconnected', handleDisconnected)
  socketService.on('user-joined', handleUserJoined)
  socketService.on('user-left', handleUserLeft)
  socketService.on('joined-document', handleJoinedDocument)
  socketService.on('annotation-created', handleAnnotationCreated)
  socketService.on('annotation-updated', handleAnnotationUpdated)
  socketService.on('annotation-deleted', handleAnnotationDeleted)
  socketService.on('cursor-moved', handleCursorMoved)

  if (props.documentId) {
    socketService.joinDocument(props.documentId)
  }
})

onUnmounted(() => {
  socketService.off('connected', handleConnected)
  socketService.off('disconnected', handleDisconnected)
  socketService.off('user-joined', handleUserJoined)
  socketService.off('user-left', handleUserLeft)
  socketService.off('joined-document', handleJoinedDocument)
  socketService.off('annotation-created', handleAnnotationCreated)
  socketService.off('annotation-updated', handleAnnotationUpdated)
  socketService.off('annotation-deleted', handleAnnotationDeleted)
  socketService.off('cursor-moved', handleCursorMoved)
  
  socketService.leaveDocument()
})
</script>

<style lang="scss" scoped>
.realtime-sync {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 12px;
  background: #fff;
  border-radius: 8px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.08);
}

.sync-status {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  border-radius: 6px;
  font-size: 13px;

  &.connected {
    background: #f0f9eb;
    color: #67C23A;
  }

  &.disconnected {
    background: #fef0f0;
    color: #F56C6C;
  }

  .status-icon {
    font-size: 16px;
  }
}

.online-users {
  .online-header {
    font-size: 13px;
    color: #606266;
    margin-bottom: 8px;
  }

  .user-avatars {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;

    .el-avatar {
      cursor: pointer;
      transition: transform 0.2s;

      &:hover {
        transform: scale(1.1);
      }

      &.role-admin {
        border: 2px solid #F56C6C;
      }

      &.role-editor {
        border: 2px solid #409EFF;
      }

      &.role-viewer {
        border: 2px solid #909399;
      }
    }
  }
}

.activity-log {
  max-height: 300px;
  overflow-y: auto;
  border-top: 1px solid #ebeef5;
  padding-top: 12px;

  .log-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    font-size: 13px;
    color: #606266;
    margin-bottom: 8px;
  }

  .log-list {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .log-item {
    display: flex;
    gap: 8px;
    padding: 8px;
    background: #f5f7fa;
    border-radius: 6px;
    font-size: 12px;

    .log-avatar {
      flex-shrink: 0;
    }

    .log-content {
      display: flex;
      flex-direction: column;
      gap: 2px;

      .log-user {
        font-weight: 500;
        color: #303133;
      }

      .log-action {
        color: #606266;
      }

      .log-time {
        color: #909399;
        font-size: 11px;
      }
    }
  }
}

.sync-actions {
  display: flex;
  gap: 8px;
  justify-content: flex-end;

  .el-button {
    display: flex;
    align-items: center;
    gap: 4px;
  }
}

.remote-cursor {
  position: absolute;
  pointer-events: none;
  z-index: 1000;

  .cursor-line {
    width: 2px;
    height: 20px;
    animation: blink 1s infinite;
  }

  .cursor-label {
    position: absolute;
    top: -20px;
    left: 0;
    padding: 2px 6px;
    border-radius: 4px;
    font-size: 11px;
    color: #fff;
    white-space: nowrap;
  }
}

@keyframes blink {
  0%, 50% { opacity: 1; }
  51%, 100% { opacity: 0; }
}

@media (max-width: 768px) {
  .realtime-sync {
    padding: 8px;
  }

  .activity-log {
    max-height: 200px;
  }
}
</style>
