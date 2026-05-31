class RendererMonitor {
  constructor(config) {
    this.config = Object.assign({
      appId: 'default-app',
      userId: null,
      reportUrl: 'http://localhost:8000/api/report',
      reportInterval: 30000,
      fpsSampleRate: 1000,
      maxHistory: 100,
      autoStart: true,
      maxRetries: 3
    }, config || {});

    this.config.sessionId = this._generateSessionId();
    this.fpsData = [];
    this.longTaskCount = 0;
    this.jankCount = 0;
    this.memoryData = [];
    this._observers = [];
    this._intervals = [];
    this._rafId = null;
    this._isRunning = false;
    this._lastReportTime = Date.now();

    if (this.config.autoStart) {
      this.start();
    }
  }

  _generateSessionId() {
    return 'session_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
  }

  start() {
    if (this._isRunning) {
      return;
    }

    if (typeof window === 'undefined' || typeof performance === 'undefined') {
      console.warn('[RendererMonitor] Browser environment not detected');
      return;
    }

    try {
      this._initFPSMonitor();
      this._initLongTaskMonitor();
      this._initMemoryMonitor();
      this._startAutoReport();
      this._isRunning = true;
    } catch (e) {
      console.warn('[RendererMonitor] Start failed:', e.message);
    }
  }

  _initFPSMonitor() {
    if (typeof requestAnimationFrame === 'undefined') {
      console.warn('[RendererMonitor] requestAnimationFrame not supported');
      return;
    }

    var frameCount = 0;
    var lastTime = performance.now();
    var sampleRate = this.config.fpsSampleRate;
    var maxHistory = this.config.maxHistory;
    var self = this;
    var isRunning = true;

    function measureFPS() {
      if (!isRunning) return;

      var currentTime = performance.now();
      frameCount++;

      if (currentTime - lastTime >= sampleRate) {
        var fps = Math.round((frameCount * 1000) / (currentTime - lastTime) * 100) / 100;
        self.fpsData.push({
          time: new Date().toISOString(),
          fps: fps
        });

        if (fps < 30) {
          self.jankCount++;
        }

        frameCount = 0;
        lastTime = currentTime;

        if (self.fpsData.length > maxHistory) {
          self.fpsData.shift();
        }
      }

      self._rafId = requestAnimationFrame(measureFPS);
    }

    this._rafId = requestAnimationFrame(measureFPS);

    this._stopFPS = function() {
      isRunning = false;
      if (self._rafId) {
        cancelAnimationFrame(self._rafId);
        self._rafId = null;
      }
    };
  }

  _initLongTaskMonitor() {
    if (typeof PerformanceObserver === 'undefined') {
      return;
    }

    try {
      var observer = new PerformanceObserver(function(list) {
        var entries = list.getEntries();
        for (var i = 0; i < entries.length; i++) {
          if (entries[i].duration > 50) {
            this.longTaskCount++;
          }
        }
      }.bind(this));

      observer.observe({ entryTypes: ['longtask'] });
      this._observers.push(observer);
    } catch (e) {
      console.warn('[RendererMonitor] Long task monitor init failed:', e.message);
    }
  }

  _initMemoryMonitor() {
    if (typeof performance === 'undefined' || !performance.memory) {
      return;
    }

    var maxHistory = this.config.maxHistory;
    var self = this;

    this._memoryInterval = setInterval(function() {
      try {
        var memoryInfo = performance.memory;
        self.memoryData.push({
          time: new Date().toISOString(),
          usedJSHeapSize: Math.round(memoryInfo.usedJSHeapSize / 1048576 * 100) / 100,
          totalJSHeapSize: Math.round(memoryInfo.totalJSHeapSize / 1048576 * 100) / 100,
          jsHeapSizeLimit: Math.round(memoryInfo.jsHeapSizeLimit / 1048576 * 100) / 100
        });

        if (self.memoryData.length > maxHistory) {
          self.memoryData.shift();
        }
      } catch (e) {
        // ignore
      }
    }, 5000);

    this._intervals.push(this._memoryInterval);
  }

  _startAutoReport() {
    var self = this;
    var interval = this.config.reportInterval;

    this._reportInterval = setInterval(function() {
      self._reportRendererMetrics();
    }, interval);

    this._intervals.push(this._reportInterval);
  }

  _reportRendererMetrics() {
    if (!this._isRunning) {
      return;
    }

    var currentMemory = null;
    if (this.memoryData.length > 0) {
      currentMemory = this.memoryData[this.memoryData.length - 1].usedJSHeapSize;
    }

    var avgFps = 60;
    if (this.fpsData.length > 0) {
      var sum = 0;
      for (var i = 0; i < this.fpsData.length; i++) {
        sum += this.fpsData[i].fps;
      }
      avgFps = Math.round(sum / this.fpsData.length * 100) / 100;
    }

    var reportData = {
      app_id: this.config.appId,
      user_id: this.config.userId,
      session_id: this.config.sessionId,
      page_url: typeof window !== 'undefined' ? window.location.href : '',
      user_agent: typeof navigator !== 'undefined' ? navigator.userAgent : '',
      timestamp: new Date().toISOString(),
      renderer: {
        fps: avgFps,
        memory_used: currentMemory,
        long_task_count: this.longTaskCount,
        jank_count: this.jankCount
      }
    };

    this._sendData('/renderer', reportData);
    this._resetCounters();
  }

  _resetCounters() {
    this.longTaskCount = 0;
    this.jankCount = 0;
    this.fpsData = [];
  }

  _sendData(endpoint, data, retryCount) {
    retryCount = retryCount || 0;
    var url = this.config.reportUrl + endpoint;

    try {
      if (typeof navigator !== 'undefined' && navigator.sendBeacon) {
        var blob = new Blob([JSON.stringify(data)], { type: 'application/json' });
        var success = navigator.sendBeacon(url, blob);
        if (success) {
          return;
        }
      }
    } catch (e) {
      // Fall through to fetch
    }

    if (typeof fetch !== 'undefined') {
      fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data),
        keepalive: true
      }).then(function(response) {
        if (!response.ok) {
          throw new Error('HTTP ' + response.status);
        }
      }).catch(function(err) {
        console.warn('[RendererMonitor] Report failed:', err.message);
        if (retryCount < 3) {
          setTimeout(function() {
            this._sendData(endpoint, data, retryCount + 1);
          }.bind(this), 1000 * (retryCount + 1));
        }
      }.bind(this));
    } else if (typeof XMLHttpRequest !== 'undefined') {
      try {
        var xhr = new XMLHttpRequest();
        xhr.open('POST', url, true);
        xhr.setRequestHeader('Content-Type', 'application/json');
        xhr.send(JSON.stringify(data));
      } catch (e) {
        console.warn('[RendererMonitor] XHR report failed:', e.message);
      }
    }
  }

  getCurrentMetrics() {
    var currentMemory = null;
    if (this.memoryData.length > 0) {
      currentMemory = this.memoryData[this.memoryData.length - 1].usedJSHeapSize;
    }

    var avgFps = 0;
    if (this.fpsData.length > 0) {
      var sum = 0;
      for (var i = 0; i < this.fpsData.length; i++) {
        sum += this.fpsData[i].fps;
      }
      avgFps = Math.round(sum / this.fpsData.length * 100) / 100;
    }

    return {
      fps: avgFps,
      long_task_count: this.longTaskCount,
      jank_count: this.jankCount,
      memory_used: currentMemory,
      fps_history: this.fpsData.slice(-20),
      memory_history: this.memoryData.slice(-20)
    };
  }

  manualReport() {
    if (this._isRunning) {
      this._reportRendererMetrics();
    }
  }

  stop() {
    this._isRunning = false;

    if (this._stopFPS) {
      this._stopFPS();
    }

    for (var i = 0; i < this._intervals.length; i++) {
      clearInterval(this._intervals[i]);
    }
    this._intervals = [];

    for (var j = 0; j < this._observers.length; j++) {
      try {
        this._observers[j].disconnect();
      } catch (e) {
        // ignore
      }
    }
    this._observers = [];
  }

  destroy() {
    this.stop();
    this.fpsData = [];
    this.memoryData = [];
    this.longTaskCount = 0;
    this.jankCount = 0;
  }
}

if (typeof window !== 'undefined') {
  window.RendererMonitor = RendererMonitor;
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = RendererMonitor;
}
