class PerformanceMonitor {
  constructor(config) {
    this.config = Object.assign({
      appId: 'default-app',
      userId: null,
      reportUrl: 'http://localhost:8000/api/report',
      sampleRate: 1,
      autoReport: true,
      reportOnLoad: true,
      maxRetries: 3
    }, config || {});

    this.config.sessionId = this._generateSessionId();
    this.metrics = {};
    this.observers = [];
    this._reported = false;
    this._collectStarted = false;
    this._init();
  }

  _generateSessionId() {
    return 'session_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
  }

  _shouldSample() {
    return Math.random() < this.config.sampleRate;
  }

  _init() {
    if (typeof window === 'undefined' || typeof performance === 'undefined') {
      console.warn('[PerformanceMonitor] Browser environment not detected');
      return;
    }

    this._collectStarted = true;
    this._initPaintTiming();
    this._initLCP();
    this._initNavigationTiming();

    if (this.config.reportOnLoad) {
      if (document.readyState === 'complete') {
        setTimeout(() => this._finalizeAndReport(), 1000);
      } else {
        window.addEventListener('load', () => {
          setTimeout(() => this._finalizeAndReport(), 1000);
        });
      }
    }
  }

  _initPaintTiming() {
    try {
      if (typeof PerformanceObserver === 'undefined') {
        this._collectPaintTimingFallback();
        return;
      }

      const observer = new PerformanceObserver((list) => {
        for (const entry of list.getEntries()) {
          if (entry.name === 'first-paint') {
            this.metrics.fp = Math.round(entry.startTime);
          } else if (entry.name === 'first-contentful-paint') {
            this.metrics.fcp = Math.round(entry.startTime);
          }
        }
      });

      observer.observe({ type: 'paint', buffered: true });
      this.observers.push(observer);
    } catch (e) {
      console.warn('[PerformanceMonitor] Paint timing observer failed:', e.message);
      this._collectPaintTimingFallback();
    }
  }

  _collectPaintTimingFallback() {
    try {
      const navigation = performance.getEntriesByType('navigation')[0];
      if (navigation) {
        if (typeof navigation.activationStart !== 'undefined') {
          this.metrics.fp = Math.round(navigation.activationStart);
        }
      }
    } catch (e) {
      // ignore
    }
  }

  _initLCP() {
    try {
      if (typeof PerformanceObserver === 'undefined') {
        return;
      }

      const observer = new PerformanceObserver((list) => {
        const entries = list.getEntries();
        if (entries.length > 0) {
          const lastEntry = entries[entries.length - 1];
          this.metrics.lcp = Math.round(lastEntry.startTime);
        }
      });

      observer.observe({ type: 'largest-contentful-paint', buffered: true });
      this.observers.push(observer);

      document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'hidden') {
          observer.takeRecords();
        }
      });
    } catch (e) {
      console.warn('[PerformanceMonitor] LCP observer failed:', e.message);
    }
  }

  _initNavigationTiming() {
    try {
      const navigation = performance.getEntriesByType('navigation')[0];
      if (!navigation) {
        return;
      }

      if (typeof navigation.responseStart !== 'undefined' && navigation.responseStart > 0) {
        this.metrics.ttfb = Math.round(navigation.responseStart);
      }

      if (typeof navigation.domContentLoadedEventEnd !== 'undefined') {
        this.metrics.domReady = Math.round(navigation.domContentLoadedEventEnd);
      }

      if (typeof navigation.loadEventEnd !== 'undefined' && navigation.loadEventEnd > 0) {
        this.metrics.loadTime = Math.round(navigation.loadEventEnd);
      }
    } catch (e) {
      console.warn('[PerformanceMonitor] Navigation timing collection failed:', e.message);
    }
  }

  _finalizeAndReport() {
    try {
      const navigation = performance.getEntriesByType('navigation')[0];
      if (navigation) {
        if (!this.metrics.ttfb && typeof navigation.responseStart !== 'undefined') {
          this.metrics.ttfb = Math.round(navigation.responseStart);
        }
        if (!this.metrics.domReady && typeof navigation.domContentLoadedEventEnd !== 'undefined') {
          this.metrics.domReady = Math.round(navigation.domContentLoadedEventEnd);
        }
        if (!this.metrics.loadTime && typeof navigation.loadEventEnd !== 'undefined' && navigation.loadEventEnd > 0) {
          this.metrics.loadTime = Math.round(navigation.loadEventEnd);
        }
      }
    } catch (e) {
      // ignore
    }

    if (this.config.autoReport && !this._reported) {
      this._reported = true;
      this._reportMetrics();
    }
  }

  _reportMetrics() {
    if (!this._shouldSample()) {
      return;
    }

    const hasMetrics = ['fp', 'fcp', 'lcp', 'ttfb', 'domReady', 'loadTime']
      .some(key => this.metrics[key] !== undefined);

    if (!hasMetrics) {
      console.warn('[PerformanceMonitor] No metrics collected, skipping report');
      return;
    }

    const reportData = {
      app_id: this.config.appId,
      user_id: this.config.userId,
      session_id: this.config.sessionId,
      page_url: window.location.href,
      user_agent: navigator.userAgent,
      timestamp: new Date().toISOString(),
      metrics: {
        fp: this.metrics.fp !== undefined ? this.metrics.fp : null,
        fcp: this.metrics.fcp !== undefined ? this.metrics.fcp : null,
        lcp: this.metrics.lcp !== undefined ? this.metrics.lcp : null,
        ttfb: this.metrics.ttfb !== undefined ? this.metrics.ttfb : null,
        dom_ready: this.metrics.domReady !== undefined ? this.metrics.domReady : null,
        load_time: this.metrics.loadTime !== undefined ? this.metrics.loadTime : null
      }
    };

    this._sendData('/performance', reportData);
  }

  _sendData(endpoint, data, retryCount = 0) {
    const url = this.config.reportUrl + endpoint;

    try {
      if (typeof navigator !== 'undefined' && navigator.sendBeacon) {
        const blob = new Blob([JSON.stringify(data)], { type: 'application/json' });
        const success = navigator.sendBeacon(url, blob);
        if (success) {
          console.log('[PerformanceMonitor] Data sent via beacon');
          return;
        }
      }
    } catch (e) {
      // Fall through to fetch
    }

    if (typeof fetch !== 'undefined') {
      fetch(url, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(data),
        keepalive: true
      }).then(response => {
        if (!response.ok) {
          throw new Error('HTTP ' + response.status);
        }
        console.log('[PerformanceMonitor] Data sent via fetch');
      }).catch(err => {
        console.warn('[PerformanceMonitor] Report failed:', err.message);
        if (retryCount < this.config.maxRetries) {
          setTimeout(() => this._sendData(endpoint, data, retryCount + 1), 1000 * (retryCount + 1));
        }
      });
    } else if (typeof XMLHttpRequest !== 'undefined') {
      try {
        const xhr = new XMLHttpRequest();
        xhr.open('POST', url, true);
        xhr.setRequestHeader('Content-Type', 'application/json');
        xhr.send(JSON.stringify(data));
      } catch (e) {
        console.warn('[PerformanceMonitor] XHR report failed:', e.message);
      }
    }
  }

  getMetrics() {
    return Object.assign({}, this.metrics);
  }

  collectNow() {
    this._finalizeAndReport();
    return this.getMetrics();
  }

  destroy() {
    this.observers.forEach(obs => {
      try {
        obs.disconnect();
      } catch (e) {
        // ignore
      }
    });
    this.observers = [];
    this._collectStarted = false;
  }
}

if (typeof window !== 'undefined') {
  window.PerformanceMonitor = PerformanceMonitor;
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = PerformanceMonitor;
}
