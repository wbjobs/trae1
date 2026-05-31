import { Injectable, Logger } from '@nestjs/common'
import { NotificationGateway } from './notification.gateway'

export interface NotificationPayload {
  type: 'doc_updated' | 'doc_created' | 'doc_deleted' | 'comment_added' | 'version_rollback' | 'swagger_imported' | 'project_updated' | string
  title: string
  content: string
  projectId?: number
  docId?: number
  operator?: { id: number; nickname: string }
  data?: any
}

@Injectable()
export class NotificationService {
  private readonly logger = new Logger(NotificationService.name)

  constructor(private readonly gateway: NotificationGateway) {}

  pushToUser(userId: number, payload: NotificationPayload) {
    this.gateway.notify(userId, 'notification', payload)
  }

  pushToProject(projectId: number, payload: NotificationPayload) {
    this.gateway.notifyProject(projectId, 'notification', payload)
  }

  broadcast(payload: NotificationPayload) {
    this.gateway.broadcast('notification', payload)
  }
}
