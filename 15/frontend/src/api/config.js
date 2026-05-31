import request from '@/utils/request'

export function getConfigList() {
  return request({
    url: '/configs',
    method: 'get'
  })
}

export function getConfig(id) {
  return request({
    url: `/configs/${id}`,
    method: 'get'
  })
}

export function createConfig(data) {
  return request({
    url: '/configs',
    method: 'post',
    data
  })
}

export function updateConfig(id, data) {
  return request({
    url: `/configs/${id}`,
    method: 'put',
    data
  })
}

export function deleteConfig(id) {
  return request({
    url: `/configs/${id}`,
    method: 'delete'
  })
}
