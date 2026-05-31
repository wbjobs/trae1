import { request } from '@/utils/request'

export interface ProjectData {
  name: string
  description: string
  swaggerUrl?: string
}

export const projectApi = {
  getList: (params?: { page?: number; pageSize?: number; keyword?: string }) =>
    request.get<{ list: any[]; total: number; page: number; pageSize: number }>('/projects', {
      params
    }),
  getDetail: (id: number | string) => request.get(`/projects/${id}`),
  create: (data: ProjectData) => request.post('/projects', data),
  update: (id: number | string, data: Partial<ProjectData>) => request.put(`/projects/${id}`, data),
  remove: (id: number | string) => request.delete(`/projects/${id}`),
  importSwagger: (id: number | string, data: { swaggerUrl?: string; swaggerContent?: string }) =>
    request.post(`/projects/${id}/import-swagger`, data),
  syncSwagger: (id: number | string) => request.post(`/projects/${id}/sync-swagger`)
}
