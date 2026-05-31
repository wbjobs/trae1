import { request } from '@/utils/request'

export const versionApi = {
  getList: (docId: number | string, params?: any) =>
    request.get(`/docs/${docId}/versions`, { params }),
  getDetail: (docId: number | string, versionId: number | string) =>
    request.get(`/docs/${docId}/versions/${versionId}`),
  compare: (docId: number | string, data: { leftVersionId: number | string; rightVersionId: number | string }) =>
    request.post(`/docs/${docId}/versions/compare`, data),
  rollback: (docId: number | string, versionId: number | string) =>
    request.post(`/docs/${docId}/versions/${versionId}/rollback`),
  create: (docId: number | string, data: { snapshot: any; remark?: string }) =>
    request.post(`/docs/${docId}/versions`, data)
}
