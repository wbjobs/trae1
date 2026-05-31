import { request } from '@/utils/request'

export interface DocData {
  projectId: number | string
  title: string
  description?: string
  method?: string
  path?: string
  contentType?: string
  requestParams?: any
  requestBody?: any
  response?: any
  tags?: string[]
  content?: string
  category?: string
}

export const docApi = {
  getList: (projectId: number | string, params?: any) =>
    request.get(`/projects/${projectId}/docs`, { params }),
  getDetail: (id: number | string) => request.get(`/docs/${id}`),
  create: (data: DocData) => request.post('/docs', data),
  update: (id: number | string, data: Partial<DocData>) => request.put(`/docs/${id}`, data),
  remove: (id: number | string) => request.delete(`/docs/${id}`),
  batchDelete: (ids: (number | string)[]) => request.post('/docs/batch-delete', { ids }),
  batchRemove: (ids: (number | string)[]) => request.post('/docs/batch-delete', { ids }),
  moveCategory: (data: { docId: number | string; category: string }) =>
    request.post('/docs/move-category', data),
  debug: (data: {
    method: string
    url: string
    headers?: Record<string, string>
    params?: Record<string, any>
    body?: any
  }) => request.post('/docs/debug', data),
  batchImport: (projectId: number | string, data: { docs?: any[]; format?: string; raw?: any }) =>
    request.post(`/projects/${projectId}/docs/batch-import`, data),
  buildExportUrl: (projectId: number | string, format: string, ids?: number[]) => {
    const params = new URLSearchParams()
    params.append('format', format)
    if (ids?.length) params.append('ids', ids.join(','))
    return `${request.defaults.baseURL}/projects/${projectId}/docs/export?${params.toString()}`
  },
  getComments: (docId: number | string) =>
    request.get(`/docs/${docId}/comments`),
  createComment: (docId: number | string, data: {
    content: string
    parentId?: number
    anchor?: any
    mentions?: number[]
  }) => request.post(`/docs/${docId}/comments`, data),
  updateComment: (docId: number | string, id: number | string, data: {
    content?: string
    status?: 'open' | 'resolved' | 'closed'
  }) => request.put(`/docs/${docId}/comments/${id}`, data),
  upvoteComment: (docId: number | string, id: number | string) =>
    request.post(`/docs/${docId}/comments/${id}/upvote`),
  deleteComment: (docId: number | string, id: number | string) =>
    request.delete(`/docs/${docId}/comments/${id}`)
}
