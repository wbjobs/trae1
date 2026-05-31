import request from '@/utils/request'

export function getTaskList() {
  return request({
    url: '/tasks',
    method: 'get'
  })
}

export function getTask(id) {
  return request({
    url: `/tasks/${id}`,
    method: 'get'
  })
}

export function createTask(data) {
  return request({
    url: '/tasks',
    method: 'post',
    data
  })
}

export function startTask(id) {
  return request({
    url: `/tasks/${id}/start',
    method: 'post'
  })
}

export function stopTask(id) {
  return request({
    url: `/tasks/${id}/stop',
    method: 'post'
  })
}

export function deleteTask(id) {
  return request({
    url: `/tasks/${id}`,
    method: 'delete'
  })
}

export function getTaskStatistics(id) {
  return request({
    url: `/tasks/${id}/statistics',
    method: 'get'
  })
}

export function getTaskTimeline(id) {
  return request({
    url: `/tasks/${id}/timeline',
    method: 'get'
  })
}

export function getResponseTimeDistribution(id) {
  return request({
    url: `/tasks/${id}/distribution',
    method: 'get'
  })
}

export function generateReport(id, format = 'html') {
  return request({
    url: `/tasks/${id}/report`,
    method: 'post',
    params: { format }
  })
}

export function downloadReport(id, format = 'html') {
  return request({
    url: `/tasks/${id}/report/download`,
    method: 'get',
    params: { format },
    responseType: 'blob'
  })
}

export function compareTasks(taskIds) {
  return request({
    url: '/tasks/compare',
    method: 'get',
    params: { taskIds: taskIds.join(',') }
  })
}

export function getTaskHistoryByConfig(configId) {
  return request({
    url: `/tasks/config/${configId}/history`,
    method: 'get'
  })
}
