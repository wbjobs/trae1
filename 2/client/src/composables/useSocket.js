import { ref, onMounted, onUnmounted } from 'vue'
import socketService from '@/utils/socket'

export function useSocket(documentId) {
  const isConnected = ref(false)
  const onlineUsers = ref([])
  const annotations = ref([])
  const reconnecting = ref(false)
  const connectionError = ref(null)

  const handleConnected = () => {
    isConnected.value = true
    reconnecting.value = false
    connectionError.value = null
  }

  const handleDisconnected = (data) => {
    isConnected.value = false
    if (data?.reason) {
      console.warn('[useSocket] Disconnected reason:', data.reason)
    }
  }

  const handleReconnecting = (data) => {
    reconnecting.value = true
    connectionError.value = `正在重连... (第${data.attempt}次)`
  }

  const handleReconnected = () => {
    reconnecting.value = false
    connectionError.value = null
    if (documentId?.value) {
      socketService.joinDocument(documentId.value)
    }
  }

  const handleUserJoined = (data) => {
    onlineUsers.value = data.onlineUsers || []
  }

  const handleUserLeft = (data) => {
    onlineUsers.value = data.onlineUsers || []
  }

  const handleJoinedDocument = (data) => {
    onlineUsers.value = data.onlineUsers || []
  }

  const handleAnnotationCreated = (data) => {
    if (data.annotation) {
      const exists = annotations.value.some(a => a._id === data.annotation._id)
      if (!exists) {
        annotations.value.unshift(data.annotation)
      }
    }
  }

  const handleAnnotationUpdated = (data) => {
    const index = annotations.value.findIndex(a => a._id === data.annotationId)
    if (index !== -1 && data.updates) {
      annotations.value[index] = { 
        ...annotations.value[index], 
        ...data.updates,
        version: data.version || annotations.value[index].version
      }
    }
  }

  const handleAnnotationDeleted = (data) => {
    annotations.value = annotations.value.filter(a => a._id !== data.annotationId)
  }

  const handleAnnotationResolved = (data) => {
    const index = annotations.value.findIndex(a => a._id === data.annotationId)
    if (index !== -1) {
      annotations.value[index] = { 
        ...annotations.value[index], 
        status: 'resolved' 
      }
    }
  }

  onMounted(() => {
    socketService.on('connected', handleConnected)
    socketService.on('disconnected', handleDisconnected)
    socketService.on('reconnecting', handleReconnecting)
    socketService.on('reconnected', handleReconnected)
    socketService.on('user-joined', handleUserJoined)
    socketService.on('user-left', handleUserLeft)
    socketService.on('joined-document', handleJoinedDocument)
    socketService.on('annotation-created', handleAnnotationCreated)
    socketService.on('annotation-updated', handleAnnotationUpdated)
    socketService.on('annotation-deleted', handleAnnotationDeleted)
    socketService.on('annotation-resolved', handleAnnotationResolved)
    
    if (documentId?.value) {
      socketService.joinDocument(documentId.value)
    }
  })

  onUnmounted(() => {
    socketService.off('connected', handleConnected)
    socketService.off('disconnected', handleDisconnected)
    socketService.off('reconnecting', handleReconnecting)
    socketService.off('reconnected', handleReconnected)
    socketService.off('user-joined', handleUserJoined)
    socketService.off('user-left', handleUserLeft)
    socketService.off('joined-document', handleJoinedDocument)
    socketService.off('annotation-created', handleAnnotationCreated)
    socketService.off('annotation-updated', handleAnnotationUpdated)
    socketService.off('annotation-deleted', handleAnnotationDeleted)
    socketService.off('annotation-resolved', handleAnnotationResolved)
    
    socketService.leaveDocument()
  })

  const joinDocument = (id) => {
    socketService.joinDocument(id)
  }

  const leaveDocument = () => {
    socketService.leaveDocument()
  }

  const sendAnnotation = (data) => {
    socketService.sendAnnotationCreated(data)
  }

  return {
    isConnected,
    onlineUsers,
    annotations,
    reconnecting,
    connectionError,
    joinDocument,
    leaveDocument,
    sendAnnotation
  }
}
