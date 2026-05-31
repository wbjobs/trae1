import axios from 'axios'

const api = axios.create({
  baseURL: '/api',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json'
  }
})

export function fetchResources(page = 1, perPage = 20, status = '') {
  const params = { page, per_page: perPage }
  if (status) params.status = status
  return api.get('/resources', { params }).then(r => r.data)
}

export function fetchResourceDetail(infohash) {
  return api.get(`/resources/${infohash}`).then(r => r.data)
}

export function searchResources(query) {
  return api.get('/search', { params: { q: query } }).then(r => r.data)
}

export function fetchHot(hours = 24, limit = 100) {
  return api.get('/hot', { params: { hours, limit } }).then(r => r.data)
}

export function submitMagnet(magnetUri, fetchMetadata = false) {
  return api.post('/magnet/submit', {
    magnet_uri: magnetUri,
    fetch_metadata: fetchMetadata
  }).then(r => r.data)
}

export function fetchStats() {
  return api.get('/stats').then(r => r.data)
}

export default api
