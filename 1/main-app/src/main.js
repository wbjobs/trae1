import Vue from 'vue';
import App from './App.vue';
import router from './router';
import store from './store';
import ElementUI from 'element-ui';
import 'element-ui/lib/theme-chalk/index.css';
import { registerMicroApps, start, initGlobalState, loadMicroApp, prefetchApps } from 'qiankun';
import { MICRO_APPS, LIFE_CYCLES, QIANKUN_CONFIG, PRELOAD_APPS } from '@/config/micro-apps';
import '@/permission';
import logger from '@/logger';
import { API_CONFIG } from '@/config';
import Cookies from 'js-cookie';
import CommunicationManager from '@/utils/communication';
import preloader from '@/utils/preloader';
import SubAppNavigator from '@/utils/subAppNavigator';

Vue.use(ElementUI);
Vue.config.productionTip = false;
Vue.config.performance = process.env.NODE_ENV !== 'production';

const getInitialState = () => {
  const token = Cookies.get(API_CONFIG.TOKEN_KEY);
  const userInfo = localStorage.getItem('user_info');

  return {
    user: userInfo ? JSON.parse(userInfo) : null,
    token: token || null,
    permissions: [],
    theme: 'default',
    globalLoading: false,
    appLoading: {},
    appError: {},
    appRetryCount: {}
  };
};

const initialState = getInitialState();
const actions = initGlobalState(initialState);

const commManager = new CommunicationManager(actions);

actions.onGlobalStateChange((state, prev) => {
  console.log('[MainApp] Global state changed:', state);

  if (state.token) {
    store.commit('user/SET_TOKEN', state.token);
  }
  if (state.user) {
    store.commit('user/SET_USER_INFO', state.user);
  }
});

Vue.prototype.$setGlobalState = (state) => {
  const currentState = { ...initialState, ...state };
  commManager.setGlobalState(currentState);
};
Vue.prototype.$getGlobalState = () => initialState;
Vue.prototype.$commManager = commManager;
Vue.prototype.$preloader = preloader;

const subAppNavigator = new SubAppNavigator(router, commManager);
Vue.prototype.$subAppNavigator = subAppNavigator;

const customProps = {
  getGlobalState: () => initialState,
  setGlobalState: (state) => {
    const currentState = { ...initialState, ...state };
    commManager.setGlobalState(currentState);
  },
  onGlobalStateChange: (callback, fireImmediately) => {
    actions.onGlobalStateChange(callback, fireImmediately);
  },
  communicate: {
    send: (targetApp, event, data) => commManager.send(targetApp, event, data),
    broadcast: (event, data) => commManager.broadcast(event, data),
    on: (event, callback) => commManager.on(event, callback),
    off: (event, callback) => commManager.off(event, callback)
  },
  appInfo: {
    getStatus: (appName) => commManager.getAppStatus(appName),
    getStats: () => commManager.getStats()
  }
};

const appsWithProps = MICRO_APPS.map(app => ({
  ...app,
  props: {
    ...app.props,
    ...customProps
  }
}));

const enhancedLifeCycles = {
  ...LIFE_CYCLES,
  beforeLoad: [
    ...(LIFE_CYCLES.beforeLoad || []),
    app => {
      console.log(`[MainApp] Loading app: ${app.name}`);
      commManager.setGlobalState({
        appLoading: { ...initialState.appLoading, [app.name]: true },
        appError: { ...initialState.appError, [app.name]: null }
      });
      commManager.updateAppStatus(app.name, { loaded: false, mounted: false });
    }
  ],
  afterMount: [
    ...(LIFE_CYCLES.afterMount || []),
    app => {
      console.log(`[MainApp] App mounted: ${app.name}`);
      commManager.setGlobalState({
        appLoading: { ...initialState.appLoading, [app.name]: false },
        appRetryCount: { ...initialState.appRetryCount, [app.name]: 0 }
      });
      commManager.updateAppStatus(app.name, { loaded: true, mounted: true });
      commManager.resetRetryCount(app.name);
    }
  ],
  afterUnmount: [
    ...(LIFE_CYCLES.afterUnmount || []),
    app => {
      console.log(`[MainApp] App unmounted: ${app.name}`);
      commManager.setGlobalState({
        appLoading: { ...initialState.appLoading, [app.name]: false }
      });
      commManager.updateAppStatus(app.name, { mounted: false });
    }
  ]
};

registerMicroApps(appsWithProps, enhancedLifeCycles);

const initQiankun = () => {
  start(QIANKUN_CONFIG);

  if (PRELOAD_APPS.strategy === 'all') {
    setTimeout(() => {
      prefetchApps(MICRO_APPS);
      console.log('[MainApp] All apps preloaded');
    }, 2000);
  }
};

const prefetchWithStrategy = () => {
  if (PRELOAD_APPS.strategy === 'all') {
    return;
  }

  if (PRELOAD_APPS.strategy === 'onHover') {
    const menuItems = document.querySelectorAll('.sidebar-item, .menu-item');
    menuItems.forEach(item => {
      item.addEventListener('mouseenter', () => {
        const appName = item.dataset.app;
        if (appName && PRELOAD_APPS.apps.includes(appName)) {
          const appConfig = MICRO_APPS.find(app => app.name === appName);
          if (appConfig) {
            prefetchApps([appConfig]);
            console.log(`[MainApp] Prefetched app: ${appName}`);
          }
        }
      });
    });
  }
};

const handleAppError = (appName, error) => {
  console.error(`[MainApp] App ${appName} error:`, error);

  const currentRetryCount = initialState.appRetryCount[appName] || 0;
  const maxRetries = 3;

  if (currentRetryCount < maxRetries) {
    const newRetryCount = currentRetryCount + 1;

    commManager.setGlobalState({
      appError: { ...initialState.appError, [appName]: error.message },
      appRetryCount: { ...initialState.appRetryCount, [appName]: newRetryCount },
      appLoading: { ...initialState.appLoading, [appName]: false }
    });

    commManager.updateAppStatus(appName, { lastError: error.message, retryCount: newRetryCount });

    const delay = Math.min(1000 * Math.pow(2, newRetryCount - 1), 10000);
    console.log(`[MainApp] Retrying app ${appName} in ${delay}ms (attempt ${newRetryCount}/${maxRetries})`);

    setTimeout(() => {
      const appConfig = MICRO_APPS.find(app => app.name === appName);
      if (appConfig) {
        try {
          loadMicroApp(appConfig, enhancedLifeCycles);
        } catch (retryError) {
          handleAppError(appName, retryError);
        }
      }
    }, delay);
  } else {
    console.error(`[MainApp] App ${appName} failed after ${maxRetries} retries`);
    commManager.setGlobalState({
      appError: { ...initialState.appError, [appName]: error.message }
    });
  }
};

window.addEventListener('error', (event) => {
  if (event.message?.includes('qiankun') || event.message?.includes('micro-app')) {
    console.error('[MainApp] Micro app error:', event.error);
  }
});

window.addEventListener('unhandledrejection', (event) => {
  if (event.reason?.message?.includes('qiankun') || event.reason?.message?.includes('micro-app')) {
    console.error('[MainApp] Unhandled micro app error:', event.reason);
  }
});

if (document.readyState === 'complete') {
  initQiankun();
  prefetchWithStrategy();
} else {
  window.addEventListener('load', () => {
    initQiankun();
    prefetchWithStrategy();
  });
}

logger.init();

new Vue({
  router,
  store,
  render: h => h(App)
}).$mount('#app');

export default actions;
