import request from '@/utils/request';

export function login(data) {
  return request({
    url: '/api/auth/login',
    method: 'post',
    data
  });
}

export function logout() {
  return request({
    url: '/api/auth/logout',
    method: 'post'
  });
}

export function getUserInfo() {
  return request({
    url: '/api/auth/userinfo',
    method: 'get'
  });
}

export function refreshToken(data) {
  return request({
    url: '/api/auth/refresh',
    method: 'post',
    data
  });
}
