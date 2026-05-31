import { defineStore } from 'pinia'
import { login as apiLogin, register as apiRegister, getUserProfile, updateProfile as apiUpdateProfile } from '@/utils/api'

export const useUserStore = defineStore('user', {
  state: () => ({
    user: null,
    token: localStorage.getItem('token') || null,
    isLoggedIn: false,
    isLoading: false
  }),

  getters: {
    username: (state) => state.user?.username || '',
    userRole: (state) => state.user?.role || 'viewer',
    isAdmin: (state) => state.user?.role === 'admin',
    isEditor: (state) => state.user?.role === 'editor' || state.user?.role === 'admin'
  },

  actions: {
    async login(credentials) {
      this.isLoading = true
      try {
        const response = await apiLogin(credentials)
        this.token = response.data.token
        this.user = response.data.user
        this.isLoggedIn = true
        localStorage.setItem('token', this.token)
        return response
      } finally {
        this.isLoading = false
      }
    },

    async register(userData) {
      this.isLoading = true
      try {
        const response = await apiRegister(userData)
        this.token = response.data.token
        this.user = response.data.user
        this.isLoggedIn = true
        localStorage.setItem('token', this.token)
        return response
      } finally {
        this.isLoading = false
      }
    },

    logout() {
      this.user = null
      this.token = null
      this.isLoggedIn = false
      localStorage.removeItem('token')
    },

    async initUser() {
      if (this.token && !this.user) {
        try {
          const response = await getUserProfile()
          this.user = response.data
          this.isLoggedIn = true
        } catch (error) {
          this.logout()
        }
      } else if (this.token && this.user) {
        this.isLoggedIn = true
      }
    },

    async updateProfile(data) {
      this.isLoading = true
      try {
        const response = await apiUpdateProfile(data)
        this.user = response.data
        return response
      } finally {
        this.isLoading = false
      }
    }
  }
})
