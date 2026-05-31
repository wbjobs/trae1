import { request } from '@/utils/request'

export const memberApi = {
  getList: (params?: any) => request.get('/members', { params }),
  invite: (data: { email: string; role: string; projectId?: number | string }) =>
    request.post('/members/invite', data),
  updateRole: (id: number | string, data: { role: string }) =>
    request.put(`/members/${id}/role`, data),
  remove: (id: number | string) => request.delete(`/members/${id}`),
  getProjectMembers: (projectId: number | string) =>
    request.get(`/projects/${projectId}/members`)
}

export const permissionApi = {
  hasPermission: (resource: string, action: string) =>
    request.get('/permissions/check', { params: { resource, action } }),
  getMyPermissions: () => request.get('/permissions/my')
}
