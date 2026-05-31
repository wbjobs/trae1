import request from './request'

export const login = (data) => request.post('/auth/login', data)
export const register = (data) => request.post('/auth/register', data)
export const logout = () => request.post('/auth/logout')
export const getUserProfile = () => request.get('/auth/me')
export const updateProfile = (data) => request.put('/auth/profile', data)
export const changePassword = (data) => request.put('/auth/password', data)

export const getDocuments = (params) => request.get('/documents', { params })
export const getDocument = (id) => request.get(`/documents/${id}`)
export const createDocument = (data) => request.post('/documents', data)
export const updateDocument = (id, data, version = null) => {
  const config = {}
  if (version !== null) {
    config.headers = { 'x-expected-version': version }
  }
  return request.put(`/documents/${id}`, data, config)
}
export const deleteDocument = (id, version = null) => {
  const config = {}
  if (version !== null) {
    config.headers = { 'x-expected-version': version }
  }
  return request.delete(`/documents/${id}`, config)
}

export const addCollaborator = (documentId, data) => request.post(`/documents/${documentId}/collaborators`, data)
export const removeCollaborator = (documentId, collaboratorId) => request.delete(`/documents/${documentId}/collaborators/${collaboratorId}`)

export const getDocumentVersions = (documentId, params) => request.get(`/documents/${documentId}/versions`, { params })
export const getVersion = (versionId) => request.get(`/documents/versions/${versionId}`)
export const restoreVersion = (versionId) => request.post(`/documents/versions/${versionId}/restore`)

export const getAnnotations = (documentId, params) => request.get(`/annotations/document/${documentId}`, { params })
export const getAnnotation = (id) => request.get(`/annotations/${id}`)
export const createAnnotation = (data) => request.post('/annotations', data)
export const updateAnnotation = (id, data, version = null) => {
  const config = {}
  if (version !== null) {
    config.headers = { 'x-expected-version': version }
  }
  return request.put(`/annotations/${id}`, data, config)
}
export const deleteAnnotation = (id, version = null) => {
  const config = {}
  if (version !== null) {
    config.headers = { 'x-expected-version': version }
  }
  return request.delete(`/annotations/${id}`, config)
}

export const resolveAnnotation = (id, version = null) => {
  const config = {}
  if (version !== null) {
    config.headers = { 'x-expected-version': version }
  }
  return request.post(`/annotations/${id}/resolve`, {}, config)
}
export const unresolveAnnotation = (id) => request.post(`/annotations/${id}/unresolve`)
export const replyAnnotation = (id, data) => request.post(`/annotations/${id}/reply`, data)
export const addReaction = (id, data) => request.post(`/annotations/${id}/reaction`, data)

export const getAnnotationStats = (documentId) => request.get(`/annotations/document/${documentId}/stats`)
export const searchAnnotations = (documentId, params) => request.get(`/annotations/document/${documentId}/search`, { params })
export const exportAnnotations = (documentId, params) => request.get(`/annotations/document/${documentId}/export`, { 
  params,
  responseType: 'blob'
})

export const getDocumentPermissions = (documentId) => request.get(`/permissions/document/${documentId}`)
export const grantPermission = (data) => request.post('/permissions/grant', data)
export const revokePermission = (data) => request.post('/permissions/revoke', data)
export const checkPermission = (data) => request.post('/permissions/check', data)
export const getUserPermissions = (userId, params) => request.get(`/permissions/user/${userId}`, { params })
export const getResourcePermissions = (resourceType, resourceId) => request.get(`/permissions/resource/${resourceType}/${resourceId}`)
