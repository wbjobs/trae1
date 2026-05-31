export interface User {
  _id: string
  username: string
  email: string
  role: 'admin' | 'editor' | 'viewer'
  avatar?: string
  status: 'active' | 'inactive' | 'banned'
  createdAt: string
  updatedAt: string
}

export interface Document {
  _id: string
  title: string
  content: string
  type: 'doc' | 'pdf' | 'txt' | 'markdown'
  owner: User | string
  collaborators: Collaborator[]
  status: 'draft' | 'published' | 'archived'
  version: number
  tags: string[]
  metadata: DocumentMetadata
  createdAt: string
  updatedAt: string
}

export interface Collaborator {
  user: User | string
  permission: 'view' | 'comment' | 'edit'
  addedAt: string
}

export interface DocumentMetadata {
  size?: number
  wordCount?: number
  lastModifiedBy?: User | string
}

export interface Annotation {
  _id: string
  documentId: string
  author: User | string
  type: 'highlight' | 'comment' | 'sticky-note' | 'underline' | 'suggestion'
  content: string
  position: AnnotationPosition
  visibility: 'public' | 'private' | 'selected'
  visibleTo: (User | string)[]
  status: 'active' | 'resolved' | 'deleted'
  resolvedAt?: string
  resolvedBy?: User | string
  parentId?: string
  replies: (Annotation | string)[]
  mentions: (User | string)[]
  reactions: Reaction[]
  color: string
  version: number
  createdAt: string
  updatedAt: string
}

export interface AnnotationPosition {
  start: number
  end: number
  selectionText?: string
  page?: number
  coordinates?: {
    x: number
    y: number
  }
}

export interface Reaction {
  user: User | string
  emoji: string
  createdAt: string
}

export interface DocumentVersion {
  _id: string
  documentId: string
  version: number
  title: string
  content: string
  createdBy: User | string
  changeDescription?: string
  annotations: (Annotation | string)[]
  metadata: DocumentMetadata
  createdAt: string
}

export interface Permission {
  _id: string
  userId: User | string
  resourceType: 'document' | 'annotation' | 'system'
  resourceId: string
  action: 'read' | 'write' | 'delete' | 'manage' | 'comment' | 'resolve'
  granted: boolean
  expiresAt?: string
  conditions?: Record<string, any>
  createdAt: string
}

export interface ApiResponse<T> {
  code: number
  message: string
  data: T
}

export interface PaginatedResponse<T> {
  code: number
  message: string
  data: {
    list: T[]
    total: number
    page: number
    limit: number
    totalPages: number
  }
}

export interface OnlineUser {
  id: string
  username: string
  role: string
  socketId: string
}
