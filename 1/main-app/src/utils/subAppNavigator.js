import { MICRO_APPS } from '@/config/micro-apps';

class SubAppNavigator {
  constructor(router, commManager) {
    this.router = router;
    this.commManager = commManager;
    this.currentApp = null;
    this.navigationHistory = [];
    this.maxHistoryLength = 50;
  }

  async navigateTo(appName, options = {}) {
    const { path = '/', replace = false, query = {} } = options;

    const appConfig = MICRO_APPS.find(app => app.name === appName);
    if (!appConfig) {
      console.error(`[Navigator] App not found: ${appName}`);
      return false;
    }

    try {
      const appStatus = this.commManager.getAppStatus(appName);

      if (!appStatus?.loaded) {
        console.log(`[Navigator] App ${appName} not loaded, prefetching...`);
        await this.commManager.sendMessageWithRetry({
          target: appName,
          event: 'prefetch',
          data: { url: appConfig.entry },
          timestamp: Date.now()
        });
      }

      const fullPath = `${appConfig.activeRule}${path === '/' ? '' : path}`;

      const navigationOptions = {
        path: fullPath,
        query
      };

      if (replace) {
        await this.router.replace(navigationOptions);
      } else {
        await this.router.push(navigationOptions);
      }

      this.currentApp = appName;
      this.navigationHistory.push({
        appName,
        path,
        timestamp: Date.now()
      });

      if (this.navigationHistory.length > this.maxHistoryLength) {
        this.navigationHistory.shift();
      }

      console.log(`[Navigator] Navigated to: ${appName}${path}`);
      return true;
    } catch (error) {
      console.error(`[Navigator] Navigation failed for ${appName}:`, error);
      return false;
    }
  }

  async navigateBack(defaultPath = '/dashboard') {
    if (this.navigationHistory.length > 1) {
      this.navigationHistory.pop();
      const previous = this.navigationHistory[this.navigationHistory.length - 1];

      if (previous) {
        return this.navigateTo(previous.appName, {
          path: previous.path,
          replace: true
        });
      }
    }

    return this.router.push(defaultPath);
  }

  getCurrentApp() {
    return this.currentApp;
  }

  getNavigationHistory() {
    return [...this.navigationHistory];
  }

  async switchApp(appName, options = {}) {
    if (this.currentApp === appName) {
      return true;
    }

    return this.navigateTo(appName, options);
  }

  isAppAccessible(appName) {
    const appStatus = this.commManager.getAppStatus(appName);
    return appStatus?.loaded || appStatus?.mounted;
  }

  getAccessibleApps() {
    return MICRO_APPS.filter(app => this.isAppAccessible(app.name));
  }
}

export default SubAppNavigator;
