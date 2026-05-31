import axios from 'axios';

const client = axios.create({ baseURL: '/api/v1' });

export const policyAPI = {
  list: () => client.get('/policies'),
  create: (data) => client.post('/policies', data),
  update: (id, data) => client.put(`/policies/${id}`, data),
  remove: (id) => client.delete(`/policies/${id}`),
  test: (data) => client.post('/policies/test', data),
};

export const identityAPI = {
  list: () => client.get('/identities'),
  create: (data) => client.post('/identities', data),
  remove: (id) => client.delete(`/identities/${id}`),
};

export const svidAPI = {
  list: () => client.get('/svids'),
  jwt: (audience) => client.get('/svids/jwt', { params: { audience } }),
  degrade: () => client.get('/status/degrade'),
};

export const auditAPI = {
  list: () => client.get('/audit'),
};
