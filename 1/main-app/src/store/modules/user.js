import { login, logout, getUserInfo } from '@/api/user';
import { API_CONFIG, PERMISSION_CONFIG } from '@/config';
import Cookies from 'js-cookie';
import router, { resetRouter } from '@/router';

const state = {
  token: Cookies.get(API_CONFIG.TOKEN_KEY) || '',
  userInfo: null,
  roles: []
};

const mutations = {
  SET_TOKEN: (state, token) => {
    state.token = token;
    Cookies.set(API_CONFIG.TOKEN_KEY, token);
  },
  SET_USER_INFO: (state, userInfo) => {
    state.userInfo = userInfo;
    state.roles = userInfo?.roles || [];
  },
  CLEAR_TOKEN: (state) => {
    state.token = '';
    state.userInfo = null;
    state.roles = [];
    Cookies.remove(API_CONFIG.TOKEN_KEY);
  }
};

const actions = {
  login({ commit }, loginForm) {
    return new Promise((resolve, reject) => {
      login(loginForm)
        .then(response => {
          const { token } = response.data;
          commit('SET_TOKEN', token);
          resolve();
        })
        .catch(error => {
          reject(error);
        });
    });
  },

  getUserInfo({ commit, state }) {
    return new Promise((resolve, reject) => {
      getUserInfo(state.token)
        .then(response => {
          const { user } = response.data;
          if (!user) {
            reject(new Error('获取用户信息失败'));
            return;
          }
          commit('SET_USER_INFO', user);
          resolve(user);
        })
        .catch(error => {
          reject(error);
        });
    });
  },

  logout({ commit, dispatch }) {
    return new Promise((resolve, reject) => {
      logout()
        .then(() => {
          commit('CLEAR_TOKEN');
          resetRouter();
          dispatch('permission/resetPermissions', null, { root: true });
          resolve();
        })
        .catch(error => {
          reject(error);
        });
    });
  },

  resetToken({ commit }) {
    return new Promise(resolve => {
      commit('CLEAR_TOKEN');
      resolve();
    });
  }
};

export default {
  namespaced: true,
  state,
  mutations,
  actions
};
