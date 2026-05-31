import { io } from 'socket.io-client'

class SocketService {
  constructor() {
    this.socket = null
    this.documentId = null
    this.listeners = new Map()
    this.reconnectAttempts = 0
    this.maxReconnectAttempts = 10
    this.heartbeatInterval = null
    this.isConnecting = false
    this.queue = []
    this.messageCache = new Map()
    this.batchQueue = []
    this.batchTimer = null
    this.lastActivityTime = Date.now()
    this.idleThreshold = 60000
  }

  connect(token) {
    if (this.socket?.connected || this.isConnecting) {
      return this.socket
    }

    this.isConnecting = true

    this.socket = io({
      auth: { token },
      transports: ['websocket', 'polling'],
      reconnection: true,
      reconnectionAttempts: this.maxReconnectAttempts,
      reconnectionDelay: 1000,
      reconnectionDelayMax: 10000,
      timeout: 20000,
      forceNew: true,
      upgradeTimeout: 10000
    })

    this.socket.on('connect', () => {
      console.log('[Socket] Connected successfully')
      this.isConnecting = false
      this.reconnectAttempts = 0
      this.startHeartbeat()
      this.flushQueue()
      this.emit('connected')
    })

    this.socket.on('disconnect', (reason) => {
      console.log('[Socket] Disconnected:', reason)
      this.stopHeartbeat()
      this.emit('disconnected', { reason })
      
      if (reason === 'io server disconnect') {
        console.log('[Socket] Server forced disconnect, attempting reconnect...')
        this.socket.connect()
      }
    })

    this.socket.on('connect_error', (error) => {
      console.error('[Socket] Connection error:', error.message)
      this.isConnecting = false
      this.reconnectAttempts++
      this.emit('connection-error', { error, attempt: this.reconnectAttempts })
      
      if (this.reconnectAttempts >= this.maxReconnectAttempts) {
        console.error('[Socket] Max reconnection attempts reached')
        this.emit('max-reconnect-attempts')
      }
    })

    this.socket.on('reconnect', (attemptNumber) => {
      console.log('[Socket] Reconnected after', attemptNumber, 'attempts')
      this.reconnectAttempts = 0
      this.isConnecting = false
      this.startHeartbeat()
      
      if (this.documentId) {
        this.joinDocument(this.documentId)
      }
      
      this.flushBatchQueue()
      this.emit('reconnected', { attempt: attemptNumber })
    })

    this.socket.on('reconnect_attempt', (attemptNumber) => {
      console.log('[Socket] Reconnection attempt:', attemptNumber)
      this.emit('reconnecting', { attempt: attemptNumber })
    })

    this.socket.on('reconnect_error', (error) => {
      console.error('[Socket] Reconnection error:', error)
    })

    this.socket.on('reconnect_failed', () => {
      console.error('[Socket] Reconnection failed')
      this.emit('reconnect-failed')
    })

    this.setupDocumentEvents()
    
    return this.socket
  }

  disconnect() {
    this.stopHeartbeat()
    this.stopBatchTimer()
    this.clearMessageCache()
    if (this.documentId) {
      this.leaveDocument()
    }
    if (this.socket) {
      this.socket.removeAllListeners()
      this.socket.disconnect()
      this.socket = null
    }
    this.isConnecting = false
  }

  startHeartbeat() {
    this.stopHeartbeat()
    this.heartbeatInterval = setInterval(() => {
      if (this.socket?.connected) {
        const idleTime = Date.now() - this.lastActivityTime
        
        if (idleTime > this.idleThreshold) {
          console.log('[Socket] Client idle, sending heartbeat')
        }
        
        this.socket.emit('heartbeat', { timestamp: Date.now() }, (response) => {
          if (!response || response.status !== 'ok') {
            console.warn('[Socket] Heartbeat failed')
          }
        })
      }
    }, 20000)
  }

  stopHeartbeat() {
    if (this.heartbeatInterval) {
      clearInterval(this.heartbeatInterval)
      this.heartbeatInterval = null
    }
  }

  generateMessageId() {
    return `msg_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`
  }

  isDuplicateMessage(messageId) {
    if (!messageId) return false
    
    const now = Date.now()
    if (this.messageCache.has(messageId)) {
      const cachedTime = this.messageCache.get(messageId)
      if (now - cachedTime < 5000) {
        return true
      }
    }
    
    this.messageCache.set(messageId, now)
    
    if (this.messageCache.size > 100) {
      const oldestKey = this.messageCache.keys().next().value
      this.messageCache.delete(oldestKey)
    }
    
    return false
  }

  clearMessageCache() {
    this.messageCache.clear()
  }

  addToBatchQueue(action) {
    this.batchQueue.push(action)
    
    if (this.batchQueue.length >= 10) {
      this.flushBatchQueue()
    } else if (!this.batchTimer) {
      this.batchTimer = setTimeout(() => {
        this.flushBatchQueue()
      }, 2000)
    }
  }

  stopBatchTimer() {
    if (this.batchTimer) {
      clearTimeout(this.batchTimer)
      this.batchTimer = null
    }
  }

  flushBatchQueue() {
    this.stopBatchTimer()
    
    if (this.batchQueue.length === 0 || !this.socket?.connected) {
      return
    }
    
    const actions = [...this.batchQueue]
    this.batchQueue = []
    
    if (actions.length === 1) {
      const action = actions[0]
      this.socket.emit(action.event, action.data)
    } else {
      console.log(`[Socket] Batch syncing ${actions.length} actions`)
      this.socket.emit('batch-sync', {
        documentId: this.documentId,
        actions
      })
    }
  }

  joinDocument(documentId) {
    if (!this.socket?.connected) {
      console.warn('[Socket] Not connected, queuing join-document')
      this.queue.push({ action: 'join', documentId })
      return
    }
    
    if (this.documentId === documentId) {
      return
    }
    
    if (this.documentId) {
      this.leaveDocument()
    }
    
    this.documentId = documentId
    console.log('[Socket] Joining document:', documentId)
    this.socket.emit('join-document', documentId)
    this.lastActivityTime = Date.now()
  }

  leaveDocument() {
    this.flushBatchQueue()
    if (this.documentId && this.socket?.connected) {
      console.log('[Socket] Leaving document:', this.documentId)
      this.socket.emit('leave-document', this.documentId)
      this.documentId = null
    }
  }

  flushQueue() {
    while (this.queue.length > 0) {
      const item = this.queue.shift()
      if (item.action === 'join') {
        this.joinDocument(item.documentId)
      }
    }
  }

  setupDocumentEvents() {
    if (!this.socket) return

    this.socket.on('user-joined', (data) => {
      this.lastActivityTime = Date.now()
      this.emit('user-joined', data)
    })

    this.socket.on('user-left', (data) => {
      this.lastActivityTime = Date.now()
      this.emit('user-left', data)
    })

    this.socket.on('joined-document', (data) => {
      this.lastActivityTime = Date.now()
      this.emit('joined-document', data)
    })

    this.socket.on('annotation-created', (data) => {
      this.lastActivityTime = Date.now()
      if (!this.isDuplicateMessage(data?.messageId)) {
        this.emit('annotation-created', data)
      }
    })

    this.socket.on('annotation-updated', (data) => {
      this.lastActivityTime = Date.now()
      if (!this.isDuplicateMessage(data?.messageId)) {
        this.emit('annotation-updated', data)
      }
    })

    this.socket.on('annotation-deleted', (data) => {
      this.lastActivityTime = Date.now()
      if (!this.isDuplicateMessage(data?.messageId)) {
        this.emit('annotation-deleted', data)
      }
    })

    this.socket.on('annotation-resolved', (data) => {
      this.lastActivityTime = Date.now()
      if (!this.isDuplicateMessage(data?.messageId)) {
        this.emit('annotation-resolved', data)
      }
    })

    this.socket.on('annotation-replied', (data) => {
      this.lastActivityTime = Date.now()
      if (!this.isDuplicateMessage(data?.messageId)) {
        this.emit('annotation-replied', data)
      }
    })

    this.socket.on('cursor-moved', (data) => {
      this.emit('cursor-moved', data)
    })

    this.socket.on('document-edited', (data) => {
      this.lastActivityTime = Date.now()
      this.emit('document-edited', data)
    })

    this.socket.on('document-restored', (data) => {
      this.lastActivityTime = Date.now()
      this.emit('document-restored', data)
    })

    this.socket.on('collaborator-added', (data) => {
      this.lastActivityTime = Date.now()
      this.emit('collaborator-added', data)
    })

    this.socket.on('collaborator-removed', (data) => {
      this.lastActivityTime = Date.now()
      this.emit('collaborator-removed', data)
    })
  }

  sendAnnotationCreated(data, callback) {
    this.lastActivityTime = Date.now()
    const messageId = this.generateMessageId()
    
    if (this.socket?.connected) {
      this.socket.emit('create-annotation', {
        ...data,
        messageId
      }, callback)
    } else {
      this.queue.push({ action: 'send', event: 'create-annotation', data: { ...data, messageId } })
    }
  }

  sendAnnotationUpdated(data, callback) {
    this.lastActivityTime = Date.now()
    const messageId = this.generateMessageId()
    
    if (this.socket?.connected) {
      this.socket.emit('update-annotation', {
        ...data,
        messageId
      }, callback)
    } else {
      this.queue.push({ action: 'send', event: 'update-annotation', data: { ...data, messageId } })
    }
  }

  sendAnnotationDeleted(data, callback) {
    this.lastActivityTime = Date.now()
    const messageId = this.generateMessageId()
    
    if (this.socket?.connected) {
      this.socket.emit('delete-annotation', {
        ...data,
        messageId
      }, callback)
    } else {
      this.queue.push({ action: 'send', event: 'delete-annotation', data: { ...data, messageId } })
    }
  }

  sendAnnotationResolved(data, callback) {
    this.lastActivityTime = Date.now()
    const messageId = this.generateMessageId()
    
    if (this.socket?.connected) {
      this.socket.emit('resolve-annotation', {
        ...data,
        messageId
      }, callback)
    } else {
      this.queue.push({ action: 'send', event: 'resolve-annotation', data: { ...data, messageId } })
    }
  }

  sendAnnotationReplied(data, callback) {
    this.lastActivityTime = Date.now()
    const messageId = this.generateMessageId()
    
    if (this.socket?.connected) {
      this.socket.emit('reply-annotation', {
        ...data,
        messageId
      }, callback)
    } else {
      this.queue.push({ action: 'send', event: 'reply-annotation', data: { ...data, messageId } })
    }
  }

  sendCursorMove(data) {
    if (this.socket?.connected) {
      this.socket.emit('cursor-move', data)
    }
  }

  sendDocumentEdit(data) {
    this.lastActivityTime = Date.now()
    if (this.socket?.connected) {
      this.socket.emit('document-edit', data)
    }
  }

  on(event, callback) {
    if (!this.listeners.has(event)) {
      this.listeners.set(event, [])
    }
    this.listeners.get(event).push(callback)
  }

  off(event, callback) {
    if (!this.listeners.has(event)) return
    
    if (!callback) {
      this.listeners.delete(event)
      return
    }
    
    const callbacks = this.listeners.get(event)
    const index = callbacks.indexOf(callback)
    if (index > -1) {
      callbacks.splice(index, 1)
    }
  }

  emit(event, data) {
    if (!this.listeners.has(event)) return
    
    this.listeners.get(event).forEach(callback => {
      try {
        callback(data)
      } catch (error) {
        console.error(`[Socket] Error in event listener for ${event}:`, error)
      }
    })
  }

  isConnected() {
    return this.socket?.connected || false
  }

  getSocketId() {
    return this.socket?.id || null
  }

  getStats() {
    return {
      connected: this.isConnected(),
      documentId: this.documentId,
      queueLength: this.queue.length,
      batchQueueLength: this.batchQueue.length,
      cachedMessages: this.messageCache.size,
      reconnectAttempts: this.reconnectAttempts
    }
  }
}

const socketService = new SocketService()

export default socketService
