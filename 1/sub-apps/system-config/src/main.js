import Vue from 'vue';
import VueRouter from 'vue-router';
import ElementUI from 'element-ui';
import 'element-ui/lib/theme-chalk/index.css';
import App from './App.vue';
import { routes } from './router';
import actions from './shared/actions';

Vue.use(VueRouter);
Vue.use(ElementUI);
Vue.config.productionTip = false;

let router = null;
let instance = null;

function render(props = {}) {
  const { container, routerBase } = props;

  router = new VueRouter({
    mode: 'history',
    base: routerBase || '/',
    routes
  });

  router.beforeEach((to, from, next) => {
    if (window.__POWERED_BY_QIANKUN__) {
      next();
      return;
    }

    const token = localStorage.getItem('access_token');
    if (!token && to.path !== '/login') {
      window.location.href = '/login';
      return;
    }
    next();
  });

  instance = new Vue({
    router,
    render: h => h(App)
  }).$mount(container ? container.querySelector('#app') : '#app');
}

if (!window.__POWERED_BY_QIANKUN__) {
  render();
}

export async function bootstrap() {
  console.log('[system-config] bootstrap');
}

export async function mount(props) {
  console.log('[system-config] mount', props);
  actions.setActions(props);

  if (props.onGlobalStateChange) {
    props.onGlobalStateChange((state) => {
      console.log('[system-config] state changed:', state);
    }, true);
  }

  render(props);
}

export async function unmount() {
  console.log('[system-config] unmount');
  if (instance) {
    instance.$destroy();
    instance.$el.innerHTML = '';
    instance = null;
  }
  router = null;
}
