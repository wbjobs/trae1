import axios from 'axios';

const API_BASE = process.env.REACT_APP_API_BASE || 'http://localhost:8000';

const api = axios.create({
    baseURL: API_BASE,
    timeout: 30000,
    headers: {
        'Content-Type': 'application/json',
    },
});

export const auditApi = {
    health: () => api.get('/api/health'),

    queryEvents: (params = {}) => {
        return api.get('/api/events', { params });
    },

    getDashboard: (days = 7, topN = 10) => {
        return api.get('/api/dashboard', { params: { days, top_n: topN } });
    },

    getTopUsers: (days = 7, topN = 10) => {
        return api.get('/api/dashboard/top-users', { params: { days, top_n: topN } });
    },

    getTrend: (days = 7) => {
        return api.get('/api/dashboard/trend', { params: { days } });
    },

    getExtensionStats: (days = 7) => {
        return api.get('/api/dashboard/extensions', { params: { days } });
    },

    ingestEvent: (event) => {
        return api.post('/api/events/manual', event);
    },

    triggerCleanup: () => {
        return api.post('/api/system/cleanup');
    },
};

export default api;
