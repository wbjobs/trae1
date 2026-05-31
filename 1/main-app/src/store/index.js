import Vue from 'vue';
import Vuex from 'vuex';
import user from './modules/user';
import permission from './modules/permission';
import app from './modules/app';

Vue.use(Vuex);

const store = new Vuex.Store({
  modules: {
    user,
    permission,
    app
  },
  getters: {
    token: state => state.user.token,
    userInfo: state => state.user.userInfo,
    permissions: state => state.permission.permissions,
    routes: state => state.permission.routes,
    sidebarRouters: state => state.app.sidebarRouters
  }
});

export default store;
