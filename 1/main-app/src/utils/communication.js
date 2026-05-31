import { MICRO_APPS } from '@/config/micro-apps';

class CommunicationManager {
  constructor(actions) {
    this.actions = actions;
    this.eventListeners = new Map();
    this.messageQueue = new Map();
    this.retryConfig = {
      maxRetries: 3,
      baseDelay: 1000,
      maxDelay: 10000
    };
    this.appStatus = new Map();
    this.messageId = 0;

    this.initAppStatus();
  }

  initAppStatus() {
    MICRO_APPS.forEach(app => {
      this.appStatus.set(app.name, {
        loaded: false,
        mounted: false,
        lastError: null,
        retryCount: 0
      });
    });
  }

  setGlobalState(state) {
    try {
      this.actions.setGlobalState(state);
      return true;
    } catch (error) {
      console.error('[CommunicationManager] setGlobalState error:', error);
      return false;
    }
  }

  onGlobalStateChange(callback, fireImmediately = false) {
    return this.actions.onGlobalStateChange(callback, fireImmediately);
  }

  send(targetApp, event, data) {
    const messageId = ++this.messageId;
    const message = {
      id: messageId,
      target: targetApp,
      event,
      data,
      timestamp: Date.now()
    };

    console.log(`[CommunicationManager] Sending message to ${targetApp}:`, event);

    return this.sendMessageWithRetry(message);
  }

  broadcast(event, data) {
    const results = [];

    MICRO_APPS.forEach(app => {
      if (this.appStatus.has(app.name)) {
        results.push(this.send(app.name, event, data));
      }
    });

    return Promise.allSettled(results);
  }

  on(event, callback) {
    if (!this.eventListeners.has(event)) {
      this.eventListeners.set(event, new Set());
    }
    this.eventListeners.get(event).add(callback);

    return () => this.off(event, callback);
  }

  off(event, callback) {
    const listeners = this.eventListeners.get(event);
    if (listeners && callback) {
      listeners.delete(callback);
    } else if (listeners) {
      this.eventListeners.delete(event);
    }
  }

  emit(event, data) {
    const listeners = this.eventListeners.get(event);
    if (listeners) {
      listeners.forEach(callback => {
        try {
          callback(data);
        } catch (error) {
          console.error(`[CommunicationManager] Event listener error for ${event}:`, error);
        }
      });
    }
  }

  async sendMessageWithRetry(message) {
    const { target } = message;
    const appStatus = this.appStatus.get(target);

    if (!appStatus) {
      console.error(`[CommunicationManager] Unknown app: ${target}`);
      return { success: false, error: 'Unknown app' };
    }

    if (!appStatus.loaded && !appStatus.mounted) {
      console.warn(`[CommunicationManager] App ${target} is not loaded, queuing message`);
      this.queueMessage(target, message);
      return { success: false, queued: true };
    }

    for (let attempt = 0; attempt <= this.retryConfig.maxRetries; attempt++) {
      try {
        const result = await this.dispatchMessage(target, message);

        if (result.success) {
          appStatus.retryCount = 0;
          appStatus.lastError = null;
          return result;
        }

        throw new Error(result.error || 'Unknown error');
      } catch (error) {
        console.error(
          `[CommunicationManager] Attempt ${attempt + 1} failed for ${target}:`,
          error.message
        );

        if (attempt < this.retryConfig.maxRetries) {
          const delay = this.calculateDelay(attempt);
          console.log(`[CommunicationManager] Retrying in ${delay}ms...`);
          await this.delay(delay);
        } else {
          appStatus.lastError = error.message;
          appStatus.retryCount = this.retryConfig.maxRetries;

          this.emit('communication-error', {
            app: target,
            error: error.message,
            message
          });

          return {
            success: false,
            error: error.message,
            retryCount: this.retryConfig.maxRetries
          };
        }
      }
    }
  }

  async dispatchMessage(targetApp, message) {
    try {
      const globalState = {
        ...this.getGlobalState(),
        communication: {
          event: message.event,
          data: message.data,
          timestamp: message.timestamp
        }
      };

      this.setGlobalState(globalState);

      this.emit('message-sent', { target: targetApp, message });

      return { success: true };
    } catch (error) {
      return { success: false, error: error.message };
    }
  }

  queueMessage(targetApp, message) {
    if (!this.messageQueue.has(targetApp)) {
      this.messageQueue.set(targetApp, []);
    }
    this.messageQueue.get(targetApp).push(message);
  }

  flushQueue(targetApp) {
    const queue = this.messageQueue.get(targetApp);
    if (queue && queue.length > 0) {
      console.log(`[CommunicationManager] Flushing ${queue.length} messages for ${targetApp}`);

      const messages = [...queue];
      this.messageQueue.set(targetApp, []);

      messages.forEach(message => {
        this.sendMessageWithRetry(message).catch(error => {
          console.error(`[CommunicationManager] Failed to flush message:`, error);
        });
      });
    }
  }

  updateAppStatus(appName, status) {
    const currentStatus = this.appStatus.get(appName);
    if (currentStatus) {
      this.appStatus.set(appName, { ...currentStatus, ...status });

      if (status.loaded || status.mounted) {
        this.flushQueue(appName);
      }
    }
  }

  getAppStatus(appName) {
    return this.appStatus.get(appName);
  }

  getGlobalState() {
    const state = {};
    this.actions.onGlobalStateChange((currentState) => {
      Object.assign(state, currentState);
    }, true);
    return state;
  }

  calculateDelay(attempt) {
    const { baseDelay, maxDelay } = this.retryConfig;
    const delay = baseDelay * Math.pow(2, attempt);
    return Math.min(delay, maxDelay);
  }

  delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }

  resetRetryCount(appName) {
    const status = this.appStatus.get(appName);
    if (status) {
      status.retryCount = 0;
      status.lastError = null;
    }
  }

  getStats() {
    const stats = {};
    this.appStatus.forEach((status, appName) => {
      stats[appName] = {
        loaded: status.loaded,
        mounted: status.mounted,
        retryCount: status.retryCount,
        lastError: status.lastError,
        queuedMessages: this.messageQueue.get(appName)?.length || 0
      };
    });
    return stats;
  }
}

export default CommunicationManager;
