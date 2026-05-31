import { getPermissions } from '@/api/permission';
import { PERMISSION_CONFIG } from '@/config';
import { asyncRoutes, constantRoutes } from '@/router';
import store from '@/store';

const state = {
  permissions: [],
  routes: [],
  addRoutes: []
};

const mutations = {
  SET_PERMISSIONS: (state, permissions) => {
    state.permissions = permissions;
  },
  SET_ROUTES: (state, routes) => {
    state.addRoutes = routes;
    state.routes = constantRoutes.concat(routes);
  },
  RESET_PERMISSIONS: (state) => {
    state.permissions = [];
    state.routes = [];
    state.addRoutes = [];
  }
};

const actions = {
  getPermissions({ commit }) {
    return new Promise((resolve, reject) => {
      getPermissions()
        .then(response => {
          const permissions = response.data?.permissions || response.data || [];
          commit('SET_PERMISSIONS', permissions);

          const accessibleRoutes = filterAsyncRoutes(asyncRoutes, permissions);
          commit('SET_ROUTES', accessibleRoutes);

          if (store.state.app) {
            store.dispatch('app/setSidebarRouters', accessibleRoutes, { root: true });
          }

          resolve(permissions);
        })
        .catch(error => {
          console.error('Get permissions error:', error);
          commit('SET_PERMISSIONS', []);
          commit('SET_ROUTES', []);
          resolve([]);
        });
    });
  },

  resetPermissions({ commit }) {
    commit('RESET_PERMISSIONS');
  }
};

function filterAsyncRoutes(routes, permissions) {
  const res = [];
  routes.forEach(route => {
    const tmp = { ...route };
    if (hasPermission(route, permissions)) {
      if (tmp.children) {
        tmp.children = filterAsyncRoutes(tmp.children, permissions);
      }
      res.push(tmp);
    }
  });
  return res;
}

function hasPermission(route, permissions) {
  const userInfo = store.getters.userInfo;

  if (userInfo && userInfo.roles) {
    const isSuperAdmin = userInfo.roles.some(role => role.code === PERMISSION_CONFIG.SUPER_ADMIN_ROLE);
    if (isSuperAdmin) {
      return true;
    }
  }

  if (route.meta && route.meta.app) {
    if (!permissions || permissions.length === 0) {
      return false;
    }
    return permissions.some(p => p.app === route.meta.app);
  }

  if (route.meta && route.meta.permission) {
    if (!permissions || permissions.length === 0) {
      return false;
    }
    return permissions.some(p => p.code === route.meta.permission);
  }

  return true;
}

export default {
  namespaced: true,
  state,
  mutations,
  actions
};
