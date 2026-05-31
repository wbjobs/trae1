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

  instance = new Vue({
    router,
    render: h => h(App)
  }).$mount(container ? container.querySelector('#app') : '#app');
}

if (!window.__POWERED_BY_QIANKUN__) {
  render();
}

export async function bootstrap() {
  console.log('[operation-log] bootstrap');
}

export async function mount(props) {
  console.log('[operation-log] mount', props);
  actions.setActions(props);

  if (props.onGlobalStateChange) {
    props.onGlobalStateChange((state) => {
      console.log('[operation-log] state changed:', state);
    }, true);
  }

  render(props);
}

export async function unmount() {
  console.log('[operation-log] unmount');
  if (instance) {
    instance.$destroy();
    instance.$el.innerHTML = '';
    instance = null;
  }
  router = null;
}
