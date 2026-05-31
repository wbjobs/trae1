import request from '@/utils/request';

export function getPermissions() {
  return request({
    url: '/api/permissions',
    method: 'get'
  });
}

export function checkPermission(app, action) {
  return request({
    url: '/api/permissions/check',
    method: 'post',
    data: { app, action }
  });
}

export function getRoles() {
  return request({
    url: '/api/roles',
    method: 'get'
  });
}

export function getRolePermissions(roleId) {
  return request({
    url: `/api/roles/${roleId}/permissions`,
    method: 'get'
  });
}
