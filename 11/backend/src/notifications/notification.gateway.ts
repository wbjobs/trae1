import {
  WebSocketGateway,
  WebSocketServer,
  SubscribeMessage,
  OnGatewayConnection,
  OnGatewayDisconnect,
  ConnectedSocket,
  MessageBody
} from '@nestjs/websockets'
import { Server, Socket } from 'socket.io'
import { JwtService } from '@nestjs/jwt'
import { Logger } from '@nestjs/common'

@WebSocketGateway({
  cors: { origin: '*' },
  namespace: '/notifications'
})
export class NotificationGateway implements OnGatewayConnection, OnGatewayDisconnect {
  private readonly logger = new Logger(NotificationGateway.name)
  private readonly userSockets = new Map<number, Set<string>>()

  @WebSocketServer()
  server: Server

  constructor(private readonly jwtService: JwtService) {}

  async handleConnection(client: Socket) {
    const token = this.extractToken(client)
    if (!token) {
      client.disconnect(true)
      return
    }
    try {
      const payload = this.jwtService.verify(token)
      const userId = payload.sub
      if (!this.userSockets.has(userId)) {
        this.userSockets.set(userId, new Set())
      }
      this.userSockets.get(userId).add(client.id)
      client.data.userId = userId
      client.join(`user:${userId}`)
      this.logger.log(`用户 ${userId} 已连接, 客户端 ${client.id}`)
    } catch (e) {
      client.disconnect(true)
    }
  }

  handleDisconnect(client: Socket) {
    const userId = client.data.userId
    if (userId && this.userSockets.has(userId)) {
      this.userSockets.get(userId).delete(client.id)
      if (this.userSockets.get(userId).size === 0) {
        this.userSockets.delete(userId)
      }
    }
    this.logger.log(`客户端 ${client.id} 已断开`)
  }

  private extractToken(client: Socket): string | null {
    const auth = client.handshake.headers?.authorization || client.handshake.auth?.token
    if (!auth) return null
    if (typeof auth === 'string' && auth.startsWith('Bearer ')) {
      return auth.slice(7)
    }
    return typeof auth === 'string' ? auth : null
  }

  @SubscribeMessage('ping')
  handlePing(@ConnectedSocket() client: Socket) {
    client.emit('pong', { timestamp: Date.now() })
  }

  notify(userId: number, event: string, data: any) {
    this.server.to(`user:${userId}`).emit(event, data)
  }

  notifyProject(projectId: number, event: string, data: any) {
    this.server.to(`project:${projectId}`).emit(event, data)
  }

  broadcast(event: string, data: any) {
    this.server.emit(event, data)
  }
}
