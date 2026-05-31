const MICRO_APPS = [
  {
    name: 'system-config',
    entry: '//localhost:8001',
    container: '#subapp-container',
    activeRule: '/system-config',
    loader: true,
    props: {
      routerBase: '/system-config'
    }
  },
  {
    name: 'data-audit',
    entry: '//localhost:8002',
    container: '#subapp-container',
    activeRule: '/data-audit',
    loader: true,
    props: {
      routerBase: '/data-audit'
    }
  },
  {
    name: 'operation-log',
    entry: '//localhost:8003',
    container: '#subapp-container',
    activeRule: '/operation-log',
    loader: true,
    props: {
      routerBase: '/operation-log'
    }
  },
  {
    name: 'role-management',
    entry: '//localhost:8004',
    container: '#subapp-container',
    activeRule: '/role-management',
    loader: true,
    props: {
      routerBase: '/role-management'
    }
  }
];

const PRELOAD_APPS = {
  strategy: 'all',
  apps: ['system-config', 'data-audit', 'operation-log', 'role-management']
};

const LIFE_CYCLES = {
  beforeLoad: [
    app => {
      console.log('[LifeCycle] before load %c%s', 'color: green;', app.name);
      app.loading = true;
    }
  ],
  beforeMount: [
    app => {
      console.log('[LifeCycle] before mount %c%s', 'color: green;', app.name);
    }
  ],
  afterMount: [
    app => {
      console.log('[LifeCycle] after mount %c%s', 'color: green;', app.name);
      app.loading = false;
    }
  ],
  afterUnmount: [
    app => {
      console.log('[LifeCycle] after unmount %c%s', 'color: green;', app.name);
    }
  ]
};

const QIANKUN_CONFIG = {
  prefetch: PRELOAD_APPS,
  sandbox: {
    experimentalStyleIsolation: true,
    strictStyleIsolation: false
  },
  singular: true,
  excludeAssetFilter: (assetUrl) => {
    const whiteList = ['/element-ui', '/element-plus', '/fonts'];
    return whiteList.some(item => assetUrl.includes(item));
  }
};

module.exports = {
  MICRO_APPS,
  LIFE_CYCLES,
  QIANKUN_CONFIG,
  PRELOAD_APPS
};
