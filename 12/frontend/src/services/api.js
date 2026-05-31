import axios from 'axios'

const request = axios.create({
  baseURL: '/api',
  timeout: 10000
})

request.interceptors.response.use(
  response => {
    const res = response.data
    if (res.code === 200) {
      return res.data
    }
    return Promise.reject(new Error(res.message || 'Error'))
  },
  error => {
    return Promise.reject(error)
  }
)

export const taskApi = {
  getAll: () => request.get('/tasks'),
  getById: (id) => request.get(`/tasks/${id}`),
  create: (data) => request.post('/tasks', data),
  update: (id, data) => request.put(`/tasks/${id}`, data),
  delete: (id) => request.delete(`/tasks/${id}`),
  start: (id) => request.post(`/tasks/${id}/start`),
  stop: (id) => request.post(`/tasks/${id}/stop`),
  execute: (id) => request.post(`/tasks/${id}/execute`),
  getByServer: (serverName) => request.get(`/tasks/server/${serverName}`),
  validateCron: (cronExpression) => request.post('/tasks/validate-cron', { cronExpression }),
  batchStart: (ids) => request.post('/tasks/batch-execute', ids),
  distribute: (id, serverNames) => request.post(`/tasks/${id}/distribute`, { serverNames })
}

export const logApi = {
  getLogs: (taskId, page, size) => request.get('/logs', { params: { taskId, page, size } }),
  getById: (id) => request.get(`/logs/${id}`),
  delete: (id) => request.delete(`/logs/${id}`),
  getAll: (page, size) => request.get('/logs/all', { params: { page, size } }),
  search: (params) => request.get('/logs', { params })
}

export const serverApi = {
  getAll: () => request.get('/servers'),
  getById: (id) => request.get(`/servers/${id}`),
  create: (data) => request.post('/servers', data),
  update: (id, data) => request.put(`/servers/${id}`, data),
  delete: (id) => request.delete(`/servers/${id}`),
  getActive: () => request.get('/servers/active'),
  updateStatus: (id, status) => request.post(`/servers/${id}/status`, null, { params: { status } }),
  updateMetrics: (id, cpuUsage, memoryUsage, diskUsage) =>
    request.post(`/servers/${id}/metrics`, null, { params: { cpuUsage, memoryUsage, diskUsage } })
}

export const alertApi = {
  getActive: () => request.get('/alerts/active'),
  getHistory: (status, alertLevel, page, size) =>
    request.get('/alerts', { params: { status, alertLevel, page, size } }),
  handle: (id, handleBy, handleRemark) =>
    request.post(`/alerts/${id}/handle`, null, { params: { handleBy, handleRemark } }),
  getStats: () => request.get('/alerts/stats'),
  check: () => request.post('/alerts/check')
}

export const statsApi = {
  getTaskStats: (taskId, startTime, endTime) =>
    request.get(`/stats/task/${taskId}`, { params: { startTime, endTime } }),
  getOverallStats: () => request.get('/stats/overall'),
  getServerDistribution: () => request.get('/stats/server-distribution')
}
