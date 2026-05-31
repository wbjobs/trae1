import axios from 'axios'

const api = axios.create({
  baseURL: '/api/v1',
  timeout: 15000
})

export const appsApi = {
  list: () => api.get('/apps')
}

export const configApi = {
  listKeys: (app, profile) => api.get(`/apps/${app}/profiles/${profile}/keys`),
  get: (app, profile, key) => api.get('/config', { params: { application: app, profile, key } }),
  publish: (payload) => api.post('/config', payload),
  delete: (app, profile, key) => api.delete('/config', { params: { application: app, profile, key } }),
  history: (app, profile, key, limit = 50) => api.get('/config/history', { params: { application: app, profile, key, limit } }),
  rollback: (payload) => api.post('/config/rollback', payload)
}

export const grayApi = {
  start: (payload) => api.post('/gray/start', payload),
  promote: (batchId, operator = 'web') =>
    api.post(`/gray/${batchId}/promote`, null, { params: { operator } }),
  cancel: (batchId, operator = 'web') =>
    api.post(`/gray/${batchId}/cancel`, null, { params: { operator } }),
  status: (batchId) => api.get(`/gray/${batchId}/status`),
  batches: (onlyActive = true) => api.get('/gray/batches', { params: { onlyActive } }),
  active: (app, profile, key) =>
    api.get('/gray/active', { params: { application: app, profile, key } }),
  clients: (app, profile) =>
    api.get('/clients', { params: { application: app, profile } })
}

export default api
