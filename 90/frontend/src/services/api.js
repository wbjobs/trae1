import axios from 'axios';

const API_BASE_URL = process.env.REACT_APP_API_URL || 'http://localhost:8080/api';

const api = axios.create({
  baseURL: API_BASE_URL,
  timeout: 30000,
});

export const configApi = {
  upload: (formData, branch = 'main', message = 'upload config', author = 'anonymous') => {
    const params = new URLSearchParams({ branch, message, author });
    return api.post(`/configs?${params.toString()}`, formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
    });
  },

  getByCID: (cid) => api.get(`/configs/${cid}`, { responseType: 'blob' }),

  getByTag: (tag) => api.get(`/configs/tag/${tag}`, { responseType: 'blob' }),

  getCommitContent: (commitId) => api.get(`/configs/commit/${commitId}/content`, { responseType: 'blob' }),
};

export const commitApi = {
  get: (id) => api.get(`/commits/${id}`),

  list: (branch) => api.get(`/commits/branch/${branch}`),

  create: (data) => api.post('/commits', data),
};

export const branchApi = {
  list: () => api.get('/branches'),

  get: (name) => api.get(`/branches/${name}`),

  create: (data) => api.post('/branches', data),

  delete: (name) => api.delete(`/branches/${name}`),
};

export const tagApi = {
  list: () => api.get('/tags'),

  get: (name) => api.get(`/tags/${name}`),

  create: (data) => api.post('/tags', data),

  delete: (name) => api.delete(`/tags/${name}`),
};

export const diffApi = {
  betweenCommits: (a, b) => api.get(`/diff/commits/${a}/${b}`),

  byCID: (cid) => api.get(`/diff/cid/${cid}`),

  astBetweenCommits: (a, b) => api.get(`/diff/ast/commits/${a}/${b}`),
};

export const mergeApi = {
  merge: (data) => api.post('/merge', data),

  preview: (data) => api.post('/merge/preview', data),

  resolve: (data) => api.post('/merge/resolve', data),

  getDefaultStrategy: () => api.get('/merge/strategy/default'),
};

export const treeApi = {
  get: (branch) => api.get(`/tree/${branch}`),
};

export default api;
