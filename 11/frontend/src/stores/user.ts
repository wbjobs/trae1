import { defineStore } from 'pinia'
import { request } from '@/utils/request'

interface UserInfo {
  id: number
  username: string
  nickname: string
  email: string
  avatar?: string
  role: 'admin' | 'editor' | 'viewer'
}

export const useUserStore = defineStore('user', {
  state: () => ({
    token: localStorage.getItem('token') || '',
    userInfo: JSON.parse(localStorage.getItem('userInfo') || 'null') as UserInfo | null
  }),
  getters: {
    isLoggedIn: (state) => !!state.token,
    isAdmin: (state) => state.userInfo?.role === 'admin'
  },
  actions: {
    async login(loginForm: { username: string; password: string }) {
      const res = await request.post<{ token: string; user: UserInfo }>('/auth/login', loginForm)
      this.token = res.data.token
      this.userInfo = res.data.user
      localStorage.setItem('token', this.token)
      localStorage.setItem('userInfo', JSON.stringify(this.userInfo))
      return res.data
    },
    async register(form: { username: string; password: string; email: string; nickname: string }) {
      const res = await request.post('/auth/register', form)
      return res.data
    },
    logout() {
      this.token = ''
      this.userInfo = null
      localStorage.removeItem('token')
      localStorage.removeItem('userInfo')
    }
  }
})
