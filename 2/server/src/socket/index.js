const { Server } = require('socket.io');
const { verifyToken } = require('../middleware/auth');

class SocketService {
  constructor(server) {
    this.io = new Server(server, {
      cors: {
        origin: '*',
        methods: ['GET', 'POST'],
        credentials: true
      },
      pingTimeout: 30000,
      pingInterval: 25000,
      transports: ['websocket', 'polling'],
      allowEIO3: true,
      maxHttpBufferSize: 1e6
    });
    
    this.documentRooms = new Map();
    this.userSockets = new Map();
    this.userDocuments = new Map();
    this.messageCache = new Map();
    this.heartbeatIntervals = new Map();
    
    this.setupMiddleware();
    this.setupEventHandlers();
    this.startStatsReporting();
  }

  setupMiddleware() {
    this.io.use(async (socket, next) => {
      try {
        const token = socket.handshake.auth.token || 
                      socket.handshake.headers.authorization?.split(' ')[1];
        
        if (!token) {
          return next(new Error('缺少认证令牌'));
        }
        
        const decoded = verifyToken(token);
        socket.user = decoded;
        socket.joinTime = Date.now();
        socket.lastActivity = Date.now();
        
        next();
      } catch (error) {
        next(new Error('无效的认证令牌'));
      }
    });
  }

  setupEventHandlers() {
    this.io.on('connection', (socket) => {
      const userId = socket.user?.id;
      console.log(`[Socket] User connected: ${userId}, socketId: ${socket.id}`);
      
      this.userSockets.set(userId, {
        socketId: socket.id,
        userId: userId,
        connectedAt: Date.now(),
        socket: socket
      });

      this.userDocuments.set(userId, new Set());
      
      this.setupSocketHeartbeat(socket);
      
      socket.on('join-document', (documentId) => {
        this.handleJoinDocument(socket, documentId);
      });
      
      socket.on('leave-document', (documentId) => {
        this.handleLeaveDocument(socket, documentId);
      });
      
      socket.on('create-annotation', (data, callback) => {
        this.handleCreateAnnotation(socket, data, callback);
      });
      
      socket.on('update-annotation', (data, callback) => {
        this.handleUpdateAnnotation(socket, data, callback);
      });
      
      socket.on('delete-annotation', (data, callback) => {
        this.handleDeleteAnnotation(socket, data, callback);
      });
      
      socket.on('resolve-annotation', (data, callback) => {
        this.handleResolveAnnotation(socket, data, callback);
      });
      
      socket.on('reply-annotation', (data, callback) => {
        this.handleReplyAnnotation(socket, data, callback);
      });
      
      socket.on('cursor-move', (data) => {
        this.handleCursorMove(socket, data);
      });
      
      socket.on('document-edit', (data) => {
        this.handleDocumentEdit(socket, data);
      });

      socket.on('batch-sync', (data, callback) => {
        this.handleBatchSync(socket, data, callback);
      });

      socket.on('heartbeat', (data, callback) => {
        socket.lastActivity = Date.now();
        if (callback) {
          callback({ status: 'ok', timestamp: Date.now(), serverTime: Date.now() });
        }
      });
      
      socket.on('disconnect', (reason) => {
        this.handleDisconnect(socket, reason);
      });

      socket.on('error', (error) => {
        console.error(`[Socket] Error for user ${userId}:`, error.message);
      });
    });

    this.io.on('connection_error', (error) => {
      console.error('[Socket] Connection error:', error.message);
    });
  }

  setupSocketHeartbeat(socket) {
    const userId = socket.user?.id;
    
    const checkInterval = setInterval(() => {
      if (!socket.connected) {
        clearInterval(checkInterval);
        this.heartbeatIntervals.delete(userId);
        return;
      }
      
      const idleTime = Date.now() - (socket.lastActivity || socket.joinTime);
      
      if (idleTime > 60000 && socket.connected) {
        console.log(`[Socket] User ${userId} idle for ${Math.floor(idleTime / 1000)}s, keeping alive`);
        socket.lastActivity = Date.now();
      }
    }, 30000);
    
    this.heartbeatIntervals.set(userId, checkInterval);
  }

  isDuplicateMessage(messageId) {
    if (!messageId) return false;
    
    const now = Date.now();
    if (this.messageCache.has(messageId)) {
      const cachedTime = this.messageCache.get(messageId);
      if (now - cachedTime < 5000) {
        return true;
      }
    }
    
    this.messageCache.set(messageId, now);
    
    if (this.messageCache.size > 1000) {
      const oldestKey = this.messageCache.keys().next().value;
      this.messageCache.delete(oldestKey);
    }
    
    return false;
  }

  handleJoinDocument(socket, documentId) {
    if (!documentId) return;

    const userId = socket.user?.id;
    console.log(`[Socket] User ${userId} joining document ${documentId}`);
    
    socket.join(`document:${documentId}`);
    
    if (!this.documentRooms.has(documentId)) {
      this.documentRooms.set(documentId, new Map());
    }
    
    const users = this.documentRooms.get(documentId);
    
    users.set(userId, {
      id: userId,
      username: socket.user.username,
      role: socket.user.role,
      socketId: socket.id,
      joinedAt: Date.now()
    });

    const userDocs = this.userDocuments.get(userId) || new Set();
    userDocs.add(documentId);
    this.userDocuments.set(userId, userDocs);
    
    const onlineUsers = Array.from(users.values());
    
    this.io.to(`document:${documentId}`).emit('user-joined', {
      documentId,
      user: {
        id: userId,
        username: socket.user.username,
        role: socket.user.role
      },
      onlineUsers
    });
    
    socket.emit('joined-document', {
      documentId,
      onlineUsers
    });

    console.log(`[Socket] User ${userId} joined document ${documentId}, total users: ${users.size}`);
  }

  handleLeaveDocument(socket, documentId) {
    if (!documentId) return;

    const userId = socket.user?.id;
    console.log(`[Socket] User ${userId} leaving document ${documentId}`);
    
    socket.leave(`document:${documentId}`);
    
    const users = this.documentRooms.get(documentId);
    if (users) {
      users.delete(userId);
      
      if (users.size === 0) {
        this.documentRooms.delete(documentId);
        console.log(`[Socket] Document ${documentId} room cleaned up`);
      } else {
        this.io.to(`document:${documentId}`).emit('user-left', {
          documentId,
          userId: userId,
          onlineUsers: Array.from(users.values())
        });
      }
    }

    const userDocs = this.userDocuments.get(userId);
    if (userDocs) {
      userDocs.delete(documentId);
    }

    console.log(`[Socket] User ${userId} left document ${documentId}`);
  }

  handleCreateAnnotation(socket, data, callback) {
    const { documentId, annotation, messageId } = data;
    const userId = socket.user?.id;
    
    if (this.isDuplicateMessage(messageId)) {
      console.log(`[Socket] Duplicate create-annotation for ${messageId}`);
      if (callback) callback({ success: true, duplicated: true });
      return;
    }
    
    console.log(`[Socket] User ${userId} creating annotation in document ${documentId}`);
    
    this.io.to(`document:${documentId}`).emit('annotation-created', {
      annotation,
      createdBy: {
        id: userId,
        username: socket.user.username
      },
      timestamp: Date.now()
    });

    if (callback) {
      callback({ success: true, timestamp: Date.now() });
    }
  }

  handleUpdateAnnotation(socket, data, callback) {
    const { documentId, annotationId, updates, version, messageId } = data;
    const userId = socket.user?.id;
    
    if (this.isDuplicateMessage(messageId)) {
      console.log(`[Socket] Duplicate update-annotation for ${messageId}`);
      if (callback) callback({ success: true, duplicated: true });
      return;
    }
    
    console.log(`[Socket] User ${userId} updating annotation ${annotationId} in document ${documentId}`);
    
    this.io.to(`document:${documentId}`).emit('annotation-updated', {
      annotationId,
      updates,
      version,
      updatedBy: {
        id: userId,
        username: socket.user.username
      },
      timestamp: Date.now()
    });

    if (callback) {
      callback({ success: true, timestamp: Date.now() });
    }
  }

  handleDeleteAnnotation(socket, data, callback) {
    const { documentId, annotationId, messageId } = data;
    const userId = socket.user?.id;
    
    if (this.isDuplicateMessage(messageId)) {
      console.log(`[Socket] Duplicate delete-annotation for ${messageId}`);
      if (callback) callback({ success: true, duplicated: true });
      return;
    }
    
    console.log(`[Socket] User ${userId} deleting annotation ${annotationId} in document ${documentId}`);
    
    this.io.to(`document:${documentId}`).emit('annotation-deleted', {
      annotationId,
      deletedBy: {
        id: userId,
        username: socket.user.username
      },
      timestamp: Date.now()
    });

    if (callback) {
      callback({ success: true, timestamp: Date.now() });
    }
  }

  handleResolveAnnotation(socket, data, callback) {
    const { documentId, annotationId, messageId } = data;
    const userId = socket.user?.id;
    
    if (this.isDuplicateMessage(messageId)) {
      if (callback) callback({ success: true, duplicated: true });
      return;
    }
    
    console.log(`[Socket] User ${userId} resolving annotation ${annotationId} in document ${documentId}`);
    
    this.io.to(`document:${documentId}`).emit('annotation-resolved', {
      annotationId,
      resolvedBy: {
        id: userId,
        username: socket.user.username
      },
      timestamp: Date.now()
    });

    if (callback) {
      callback({ success: true, timestamp: Date.now() });
    }
  }

  handleReplyAnnotation(socket, data, callback) {
    const { documentId, parentId, reply, messageId } = data;
    const userId = socket.user?.id;
    
    if (this.isDuplicateMessage(messageId)) {
      if (callback) callback({ success: true, duplicated: true });
      return;
    }
    
    console.log(`[Socket] User ${userId} replying to annotation ${parentId} in document ${documentId}`);
    
    this.io.to(`document:${documentId}`).emit('annotation-replied', {
      parentId,
      reply,
      repliedBy: {
        id: userId,
        username: socket.user.username
      },
      timestamp: Date.now()
    });

    if (callback) {
      callback({ success: true, timestamp: Date.now() });
    }
  }

  handleCursorMove(socket, data) {
    const { documentId, position } = data;
    
    if (!this.shouldBroadcastCursor(socket, documentId)) {
      return;
    }
    
    socket.lastCursorBroadcast = Date.now();
    
    socket.to(`document:${documentId}`).emit('cursor-moved', {
      userId: socket.user?.id,
      username: socket.user?.username,
      position,
      timestamp: Date.now()
    });
  }

  shouldBroadcastCursor(socket, documentId) {
    const users = this.documentRooms.get(documentId);
    if (!users || users.size < 2) {
      return false;
    }
    
    const now = Date.now();
    if (socket.lastCursorBroadcast && now - socket.lastCursorBroadcast < 100) {
      return false;
    }
    
    return true;
  }

  handleDocumentEdit(socket, data) {
    const { documentId, content, version } = data;
    
    socket.to(`document:${documentId}`).emit('document-edited', {
      documentId,
      content,
      version,
      editedBy: {
        id: socket.user?.id,
        username: socket.user?.username
      },
      timestamp: Date.now()
    });
  }

  handleBatchSync(socket, data, callback) {
    const { documentId, actions } = data;
    const userId = socket.user?.id;
    
    if (!Array.isArray(actions) || actions.length === 0) {
      if (callback) callback({ success: false, error: '无效的批量操作' });
      return;
    }
    
    console.log(`[Socket] User ${userId} batch syncing ${actions.length} actions in document ${documentId}`);
    
    const results = [];
    
    for (const action of actions) {
      if (this.isDuplicateMessage(action.messageId)) {
        results.push({ action: action.type, success: true, duplicated: true });
        continue;
      }
      
      switch (action.type) {
        case 'create-annotation':
          this.io.to(`document:${documentId}`).emit('annotation-created', {
            annotation: action.annotation,
            createdBy: { id: userId, username: socket.user.username },
            timestamp: Date.now()
          });
          results.push({ action: action.type, success: true });
          break;
          
        case 'update-annotation':
          this.io.to(`document:${documentId}`).emit('annotation-updated', {
            annotationId: action.annotationId,
            updates: action.updates,
            version: action.version,
            updatedBy: { id: userId, username: socket.user.username },
            timestamp: Date.now()
          });
          results.push({ action: action.type, success: true });
          break;
          
        case 'delete-annotation':
          this.io.to(`document:${documentId}`).emit('annotation-deleted', {
            annotationId: action.annotationId,
            deletedBy: { id: userId, username: socket.user.username },
            timestamp: Date.now()
          });
          results.push({ action: action.type, success: true });
          break;
          
        default:
          results.push({ action: action.type, success: false, error: '未知操作类型' });
      }
    }
    
    if (callback) {
      callback({ success: true, results, timestamp: Date.now() });
    }
  }

  handleDisconnect(socket, reason) {
    const userId = socket.user?.id;
    console.log(`[Socket] User disconnected: ${userId}, reason: ${reason}`);
    
    const userDocs = this.userDocuments.get(userId);
    
    if (userDocs) {
      userDocs.forEach(documentId => {
        const users = this.documentRooms.get(documentId);
        if (users) {
          users.delete(userId);
          
          if (users.size === 0) {
            this.documentRooms.delete(documentId);
          } else {
            this.io.to(`document:${documentId}`).emit('user-left', {
              documentId,
              userId: userId,
              onlineUsers: Array.from(users.values())
            });
          }
        }
      });
    }
    
    this.userSockets.delete(userId);
    this.userDocuments.delete(userId);
    
    const heartbeatInterval = this.heartbeatIntervals.get(userId);
    if (heartbeatInterval) {
      clearInterval(heartbeatInterval);
      this.heartbeatIntervals.delete(userId);
    }

    console.log(`[Socket] User ${userId} cleaned up`);
  }

  startStatsReporting() {
    setInterval(() => {
      const totalUsers = this.userSockets.size;
      const totalRooms = this.documentRooms.size;
      const totalCache = this.messageCache.size;
      
      if (totalUsers > 0) {
        console.log(`[Socket] Stats: ${totalUsers} users, ${totalRooms} rooms, ${totalCache} cached messages`);
      }
    }, 60000);
  }

  getOnlineUsers(documentId) {
    const users = this.documentRooms.get(documentId);
    return users ? Array.from(users.values()) : [];
  }

  isUserOnline(userId) {
    return this.userSockets.has(userId);
  }

  broadcastToDocument(documentId, event, data) {
    this.io.to(`document:${documentId}`).emit(event, {
      ...data,
      serverTime: Date.now()
    });
  }

  sendToUser(userId, event, data) {
    const userSocket = this.userSockets.get(userId);
    if (userSocket && userSocket.socket) {
      userSocket.socket.emit(event, {
        ...data,
        serverTime: Date.now()
      });
      return true;
    }
    return false;
  }

  getDocumentUsers(documentId) {
    const users = this.documentRooms.get(documentId);
    return users ? Array.from(users.values()) : [];
  }

  getDocumentUserCount(documentId) {
    const users = this.documentRooms.get(documentId);
    return users ? users.size : 0;
  }

  getStats() {
    return {
      totalUsers: this.userSockets.size,
      totalRooms: this.documentRooms.size,
      totalCachedMessages: this.messageCache.size
    };
  }
}

let socketService = null;

const initSocketService = (server) => {
  if (!socketService) {
    socketService = new SocketService(server);
  }
  return socketService;
};

const getSocketService = () => {
  return socketService;
};

module.exports = {
  initSocketService,
  getSocketService
};
