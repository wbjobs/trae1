import { request } from '@/utils/request'

export const authApi = {
  login: (data: { username: string; password: string }) => request.post('/auth/login', data),
  register: (data: { username: string; password: string; email: string; nickname: string }) =>
    request.post('/auth/register', data),
  profile: () => request.get('/auth/profile')
}
