import axios from 'axios';

export const api = axios.create({
  baseURL: '/api',
  timeout: 15000,
  headers: { 'Content-Type': 'application/json' },
});

export const componentsApi = {
  list: (category?: string, sortBy?: string) =>
    api.get('/components', { params: { category, sortBy } }).then((r) => r.data),
  top: (n = 10) =>
    api.get('/components/stats/top', { params: { n } }).then((r) => r.data),
  create: (payload: Record<string, unknown>) =>
    api.post('/components', payload).then((r) => r.data),
  update: (id: string, payload: Record<string, unknown>) =>
    api.put(`/components/${id}`, payload).then((r) => r.data),
  remove: (id: string) => api.delete(`/components/${id}`).then((r) => r.data),
  get: (id: string) => api.get(`/components/${id}`).then((r) => r.data),
  incDownload: (id: string) =>
    api.post(`/components/${id}/stats/download`).then((r) => r.data),
  incPreview: (id: string) =>
    api.post(`/components/${id}/stats/preview`).then((r) => r.data),
  incReference: (id: string) =>
    api.post(`/components/${id}/stats/reference`).then((r) => r.data),
};

export const versionsApi = {
  listByComponent: (componentId: string) =>
    api.get(`/versions/component/${componentId}`).then((r) => r.data),
  latest: (componentId: string) =>
    api.get(`/versions/component/${componentId}/latest`).then((r) => r.data),
  suggest: (componentId: string, bump: 'major' | 'minor' | 'patch' = 'patch') =>
    api
      .get(`/versions/component/${componentId}/suggest`, { params: { bump } })
      .then((r) => r.data),
  create: (payload: Record<string, unknown>) =>
    api.post('/versions', payload).then((r) => r.data),
  setLatest: (id: string) =>
    api.put(`/versions/${id}/latest`).then((r) => r.data),
  rollback: (id: string) =>
    api.post(`/versions/${id}/rollback`).then((r) => r.data),
  rollbackClone: (id: string, bump: 'major' | 'minor' | 'patch' = 'patch') =>
    api.post(`/versions/${id}/rollback-clone`, null, { params: { bump } }).then((r) => r.data),
  remove: (id: string) => api.delete(`/versions/${id}`).then((r) => r.data),
  get: (id: string) => api.get(`/versions/${id}`).then((r) => r.data),
};

export const dependenciesApi = {
  listByVersion: (versionId: string) =>
    api.get(`/dependencies/version/${versionId}`).then((r) => r.data),
  analyze: (versionIds: string[]) =>
    api.post('/dependencies/analyze', { versionIds }).then((r) => r.data),
  listAll: (limit = 200) =>
    api.get('/dependencies', { params: { limit } }).then((r) => r.data),
};

export const previewApi = {
  get: (versionId: string) =>
    api.get(`/preview/${versionId}`).then((r) => r.data),
};

export const docsApi = {
  get: (componentName: string) =>
    api.get(`/docs/${componentName}`).then((r) => r.data),
  markdown: (componentName: string) =>
    `/api/docs/${encodeURIComponent(componentName)}/markdown`,
};

export const bundleApi = {
  analyze: (versionId: string) =>
    api.get(`/bundle/${versionId}`).then((r) => r.data),
  compare: (a: string, b: string) =>
    api.get(`/bundle/compare/${a}/${b}`).then((r) => r.data),
};
