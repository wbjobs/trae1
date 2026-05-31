import axios from 'axios';
import { API_CONFIG } from '@/config';
import Cookies from 'js-cookie';
import { Message } from 'element-ui';
import store from '@/store';
import router from '@/router';
import logger from '@/logger';

const service = axios.create({
  baseURL: API_CONFIG.BASE_URL,
  timeout: API_CONFIG.TIMEOUT
});

service.interceptors.request.use(
  config => {
    const token = Cookies.get(API_CONFIG.TOKEN_KEY);
    if (token) {
      config.headers['Authorization'] = `Bearer ${token}`;
    }
    config.startTime = Date.now();
    return config;
  },
  error => {
    console.error('Request error:', error);
    return Promise.reject(error);
  }
);

service.interceptors.response.use(
  response => {
    const duration = Date.now() - (response.config.startTime || Date.now());
    logger.reportApiCall(
      response.config.url,
      response.config.method,
      response.status,
      duration
    );

    const res = response.data;
    if (res.code !== 200) {
      Message.error(res.message || '请求失败');

      if (res.code === 401) {
        store.dispatch('user/resetToken').then(() => {
          router.push('/login');
        });
      }
      return Promise.reject(new Error(res.message || '请求失败'));
    }
    return res;
  },
  error => {
    console.error('Response error:', error);
    const status = error.response?.status;
    const duration = Date.now() - (error.config?.startTime || Date.now());

    logger.reportApiCall(
      error.config?.url,
      error.config?.method,
      status,
      duration
    );

    if (status === 401) {
      Message.error('登录已过期，请重新登录');
      store.dispatch('user/resetToken').then(() => {
        router.push('/login');
      });
    } else if (status === 403) {
      Message.error('没有权限访问该资源');
      router.push('/403');
    } else {
      Message.error(error.message || '网络错误');
    }
    return Promise.reject(error);
  }
);

export default service;
