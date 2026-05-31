import axios from 'axios'

const api = axios.create({
  baseURL: '/api/videos',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
})

api.interceptors.response.use(
  (response) => response,
  (error) => {
    console.error('API Error:', error.response?.data || error.message)
    return Promise.reject(error)
  }
)

export const videoApi = {
  list(params = {}) {
    return api.get('/', { params })
  },

  get(id) {
    return api.get(`/${id}/`)
  },

  upload(formData, onProgress) {
    return api.post('/', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
      onUploadProgress: (e) => {
        if (onProgress && e.total) {
          onProgress(Math.round((e.loaded * 100) / e.total))
        }
      },
    })
  },

  update(id, data) {
    return api.put(`/${id}/`, data)
  },

  delete(id) {
    return api.delete(`/${id}/`)
  },

  retryTranscode(id) {
    return api.post(`/${id}/retry-transcode/`)
  },

  getToken(videoId) {
    return api.post(`/${videoId}/token/`)
  },

  getStats(videoId = null) {
    const params = videoId ? { video_id: videoId } : {}
    return api.get('/stats/', { params })
  },

  getStatsList() {
    return api.get('/stats/list/')
  },

  getKeyInfo() {
    return api.get('/key/info/')
  },

  listRecords(videoId = null) {
    const params = videoId ? { video_id: videoId } : {}
    return api.get('/playback-records/', { params })
  },

  playbackStart(videoId, token) {
    return api.post(`/${videoId}/playback/start/`, { token })
  },

  playbackUpdate(videoId, data) {
    return api.post(`/${videoId}/playback/update/`, data)
  },

  playbackEnd(videoId, data) {
    return api.post(`/${videoId}/playback/end/`, data)
  },
}

export default api
