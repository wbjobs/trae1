import axios from 'axios'
import { ElMessage } from 'element-plus'

const service = axios.create({
  baseURL: '/api',
  timeout: 60000,
  headers: {
    'Content-Type': 'application/json'
  }
})

service.interceptors.request.use(
  config => {
    return config
  },
  error => {
    console.error('Request error:', error)
    return Promise.reject(error)
  }
)

service.interceptors.response.use(
  response => {
    const res = response.data
    if (res.code !== 200) {
      ElMessage.error(res.message || '请求失败')
      return Promise.reject(new Error(res.message || '请求失败'))
    }
    return res
  },
  error => {
    console.error('Response error:', error)
    if (error.response) {
      const status = error.response.status
      if (status === 401) {
        ElMessage.error('未授权，请重新登录')
      } else if (status === 403) {
        ElMessage.error('没有访问权限')
      } else if (status === 404) {
        ElMessage.error('请求的资源不存在')
      } else if (status === 500) {
        ElMessage.error('服务器内部错误')
      } else {
        ElMessage.error(error.message || '请求失败')
      }
    } else {
      ElMessage.error('网络连接异常')
    }
    return Promise.reject(error)
  }
)

export default service
