import { LOG_CONFIG, API_CONFIG } from '@/config';
import axios from 'axios';
import Cookies from 'js-cookie';

class Logger {
  constructor() {
    this.queue = [];
    this.timer = null;
    this.enabled = LOG_CONFIG.ENABLE_REPORT;
    this.reportUrl = LOG_CONFIG.REPORT_URL;
    this.batchSize = LOG_CONFIG.BATCH_SIZE;
    this.flushInterval = LOG_CONFIG.FLUSH_INTERVAL;
  }

  init() {
    this.startAutoFlush();
    this.bindGlobalEvents();
  }

  startAutoFlush() {
    if (this.timer) clearInterval(this.timer);
    this.timer = setInterval(() => {
      this.flush();
    }, this.flushInterval);
  }

  bindGlobalEvents() {
    window.addEventListener('error', (event) => {
      this.error({
        type: 'global_error',
        message: event.message,
        filename: event.filename,
        lineno: event.lineno,
        colno: event.colno,
        stack: event.error?.stack
      });
    });

    window.addEventListener('unhandledrejection', (event) => {
      this.error({
        type: 'unhandled_rejection',
        reason: event.reason?.toString() || 'Unknown rejection'
      });
    });
  }

  info(data) {
    this.log('info', data);
  }

  warn(data) {
    this.log('warn', data);
  }

  error(data) {
    this.log('error', data);
  }

  log(level, data) {
    const logEntry = {
      level,
      timestamp: new Date().toISOString(),
      user: this.getCurrentUser(),
      url: window.location.href,
      userAgent: navigator.userAgent,
      ...data
    };

    this.queue.push(logEntry);
    console[level === 'error' ? 'error' : level === 'warn' ? 'warn' : 'log'](
      `[Logger:${level}]`,
      data
    );

    if (this.queue.length >= this.batchSize) {
      this.flush();
    }
  }

  reportUserAction(action, module, detail = {}) {
    this.info({
      type: 'user_action',
      action,
      module,
      detail
    });
  }

  reportPageView(pageName) {
    this.info({
      type: 'page_view',
      pageName
    });
  }

  reportApiCall(apiUrl, method, status, duration) {
    this.info({
      type: 'api_call',
      apiUrl,
      method,
      status,
      duration
    });
  }

  async flush() {
    if (this.queue.length === 0 || !this.enabled) return;

    const logs = [...this.queue];
    this.queue = [];

    try {
      const token = Cookies.get(API_CONFIG.TOKEN_KEY);
      await axios.post(this.reportUrl, { logs }, {
        headers: {
          'Content-Type': 'application/json',
          'Authorization': token ? `Bearer ${token}` : ''
        }
      });
    } catch (error) {
      console.error('Failed to report logs:', error);
      this.queue = [...logs, ...this.queue];
    }
  }

  getCurrentUser() {
    const userInfo = localStorage.getItem('user_info');
    if (userInfo) {
      try {
        const user = JSON.parse(userInfo);
        return {
          id: user.id,
          username: user.username,
          role: user.role
        };
      } catch (e) {
        return null;
      }
    }
    return null;
  }

  destroy() {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
    this.flush();
  }
}

const logger = new Logger();

export default logger;
