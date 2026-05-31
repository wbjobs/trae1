const API_BASE = '/api'

async function request(path, options = {}) {
  const token = localStorage.getItem('token')
  const headers = {
    'Content-Type': 'application/json',
    ...options.headers,
  }
  if (token) {
    headers['Authorization'] = `Bearer ${token}`
  }

  const response = await fetch(`${API_BASE}${path}`, {
    ...options,
    headers,
  })

  if (!response.ok) {
    const error = await response.text()
    throw new Error(error || `HTTP ${response.status}`)
  }

  if (response.status === 204) return null
  return response.json()
}

export const authApi = {
  beginRegister: (data) => request('/register/begin', {
    method: 'POST',
    body: JSON.stringify(data),
  }),
  finishRegister: (challenge, deviceName, body) => {
    const params = new URLSearchParams({ challenge, deviceName })
    return request(`/register/finish?${params}`, {
      method: 'POST',
      body: JSON.stringify(body),
    })
  },
  beginLogin: (data) => request('/login/begin', {
    method: 'POST',
    body: JSON.stringify(data),
  }),
  finishLogin: (challenge, body) => {
    const params = new URLSearchParams({ challenge })
    return request(`/login/finish?${params}`, {
      method: 'POST',
      body: JSON.stringify(body),
    })
  },
  getProfile: () => request('/user/profile'),
}

export const deviceApi = {
  getList: () => request('/user/credentials'),
  remove: (id) => request(`/user/credentials/${id}`, {
    method: 'DELETE',
  }),
}
