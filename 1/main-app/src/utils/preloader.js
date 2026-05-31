const preloadConfig = {
  criticalResources: [
    { type: 'style', href: 'https://unpkg.com/element-ui/lib/theme-chalk/index.css' },
    { type: 'script', src: 'https://unpkg.com/vue@2.7.0/dist/vue.runtime.min.js' },
    { type: 'script', src: 'https://unpkg.com/vue-router@3.5.1/dist/vue-router.min.js' },
    { type: 'script', src: 'https://unpkg.com/vuex@3.6.2/dist/vuex.min.js' },
    { type: 'script', src: 'https://unpkg.com/element-ui/lib/index.js' }
  ],

  subAppEntries: {
    'system-config': '//localhost:8001',
    'data-audit': '//localhost:8002',
    'operation-log': '//localhost:8003',
    'role-management': '//localhost:8004'
  },

  prefetchDelay: 2000,
  maxConcurrentPrefetches: 2
};

class ResourcePreloader {
  constructor(config = {}) {
    this.config = { ...preloadConfig, ...config };
    this.preloaded = new Set();
    this.prefetchQueue = [];
    this.isPrefetching = false;
    this.concurrentPrefetches = 0;
  }

  init() {
    this.preloadCriticalResources();
    this.setupIdlePrefetch();
    this.setupIntersectionObserver();
  }

  preloadCriticalResources() {
    const fragment = document.createDocumentFragment();

    this.config.criticalResources.forEach(resource => {
      if (this.preloaded.has(resource.href || resource.src)) {
        return;
      }

      let link;
      if (resource.type === 'style') {
        link = document.createElement('link');
        link.rel = 'preload';
        link.as = 'style';
        link.href = resource.href;
      } else if (resource.type === 'script') {
        link = document.createElement('link');
        link.rel = 'preload';
        link.as = 'script';
        link.href = resource.src;
      }

      if (link) {
        link.onload = () => this.preloaded.add(resource.href || resource.src);
        fragment.appendChild(link);
      }
    });

    document.head.appendChild(fragment);
  }

  setupIdlePrefetch() {
    if ('requestIdleCallback' in window) {
      requestIdleCallback(() => {
        this.prefetchSubApps();
      }, { timeout: 3000 });
    } else {
      setTimeout(() => {
        this.prefetchSubApps();
      }, this.config.prefetchDelay);
    }
  }

  setupIntersectionObserver() {
    if (!('IntersectionObserver' in window)) {
      return;
    }

    const observer = new IntersectionObserver((entries) => {
      entries.forEach(entry => {
        if (entry.isIntersecting) {
          const appName = entry.target.dataset.app;
          if (appName && !this.preloaded.has(appName)) {
            this.prefetchSubApp(appName);
          }
        }
      });
    }, { threshold: 0.1 });

    document.querySelectorAll('[data-app]').forEach(el => {
      observer.observe(el);
    });
  }

  prefetchSubApps() {
    const appNames = Object.keys(this.config.subAppEntries);

    appNames.forEach((appName, index) => {
      setTimeout(() => {
        this.prefetchSubApp(appName);
      }, index * 500);
    });
  }

  async prefetchSubApp(appName) {
    if (this.preloaded.has(appName)) {
      return;
    }

    if (this.concurrentPrefetches >= this.config.maxConcurrentPrefetches) {
      this.prefetchQueue.push(appName);
      return;
    }

    this.concurrentPrefetches++;

    try {
      const entry = this.config.subAppEntries[appName];
      if (!entry) {
        return;
      }

      await this.prefetchEntry(entry);
      this.preloaded.add(appName);

      console.log(`[Preloader] Prefetched: ${appName}`);
    } catch (error) {
      console.error(`[Preloader] Failed to prefetch ${appName}:`, error);
    } finally {
      this.concurrentPrefetches--;
      this.processQueue();
    }
  }

  async prefetchEntry(entryUrl) {
    return new Promise((resolve, reject) => {
      const link = document.createElement('link');
      link.rel = 'prefetch';
      link.href = entryUrl;
      link.onload = resolve;
      link.onerror = reject;
      document.head.appendChild(link);
    });
  }

  processQueue() {
    if (this.prefetchQueue.length > 0 && this.concurrentPrefetches < this.config.maxConcurrentPrefetches) {
      const nextApp = this.prefetchQueue.shift();
      this.prefetchSubApp(nextApp);
    }
  }

  preloadAppOnHover(appName) {
    const menuItem = document.querySelector(`[data-app="${appName}"]`);
    if (menuItem) {
      menuItem.addEventListener('mouseenter', () => {
        if (!this.preloaded.has(appName)) {
          this.prefetchSubApp(appName);
        }
      });
    }
  }

  getStats() {
    return {
      preloaded: Array.from(this.preloaded),
      queueSize: this.prefetchQueue.length,
      concurrentPrefetches: this.concurrentPrefetches
    };
  }
}

const preloader = new ResourcePreloader();

if (typeof window !== 'undefined') {
  if (document.readyState === 'complete') {
    preloader.init();
  } else {
    window.addEventListener('load', () => preloader.init());
  }
}

export default preloader;
