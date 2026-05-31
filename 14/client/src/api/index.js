import axios from 'axios'
import { ElMessage } from 'element-plus'

const request = axios.create({
  baseURL: '/api',
  timeout: 600000,
  headers: {
    'Content-Type': 'application/json'
  }
})

request.interceptors.request.use(
  config => {
    return config
  },
  error => {
    return Promise.reject(error)
  }
)

request.interceptors.response.use(
  response => {
    const res = response.data
    if (res.code && res.code !== 200) {
      ElMessage.error(res.message || '请求失败')
      return Promise.reject(new Error(res.message || '请求失败'))
    }
    return res
  },
  error => {
    if (error.response) {
      const status = error.response.status
      const message = error.response.data?.message || '请求失败'
      
      switch (status) {
        case 400:
          ElMessage.error(message || '请求参数错误')
          break
        case 401:
          ElMessage.error(message || '未授权访问')
          break
        case 403:
          ElMessage.error(message || '禁止访问')
          break
        case 404:
          ElMessage.error(message || '资源不存在')
          break
        case 410:
          ElMessage.error(message || '资源已过期')
          break
        case 500:
          ElMessage.error(message || '服务器错误')
          break
        default:
          ElMessage.error(message)
      }
    } else {
      ElMessage.error('网络连接失败')
    }
    return Promise.reject(error)
  }
)

export const fileApi = {
  upload: (formData, onProgress) => {
    return request.post('/files/upload', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
      onUploadProgress: (progressEvent) => {
        if (onProgress && progressEvent.total) {
          onProgress(Math.round((progressEvent.loaded * 100) / progressEvent.total))
        }
      }
    })
  },
  list: (params) => {
    return request.get('/files', { params })
  },
  detail: (id) => {
    return request.get(`/files/${id}`)
  },
  download: (id) => {
    return `${request.defaults.baseURL}/files/${id}/download`
  },
  preview: (id) => {
    return `${request.defaults.baseURL}/files/${id}/preview`
  },
  delete: (id, data) => {
    return request.delete(`/files/${id}`, { data })
  },
  stats: () => {
    return request.get('/files/stats/overview')
  }
}

export const chunkApi = {
  verify: (fileId, chunkIndex) => {
    return request.get('/chunks/verify', { params: { fileId, chunkIndex } })
  },
  upload: (formData, onProgress) => {
    return request.post('/chunks/upload', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
      onUploadProgress: (progressEvent) => {
        if (onProgress && progressEvent.total) {
          onProgress(Math.round((progressEvent.loaded * 100) / progressEvent.total))
        }
      }
    })
  },
  merge: (data) => {
    return request.post('/chunks/merge', data)
  },
  getUploaded: (fileId) => {
    return request.get(`/chunks/${fileId}`)
  }
}

export const shareApi = {
  create: (data) => {
    return request.post('/shares', data)
  },
  access: (shareCode, data) => {
    return request.post(`/shares/${shareCode}/access`, data)
  },
  list: (params) => {
    return request.get('/shares', { params })
  },
  detail: (id) => {
    return request.get(`/shares/${id}`)
  },
  revoke: (id, data) => {
    return request.post(`/shares/${id}/revoke`, data)
  }
}

export default request
