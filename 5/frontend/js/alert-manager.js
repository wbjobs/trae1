var AlertLevel = {
  INFO: 'info',
  WARNING: 'warning',
  CRITICAL: 'critical'
};

var AlertType = {
  PERFORMANCE: 'performance',
  ERROR: 'error',
  RENDERER: 'renderer',
  RESOURCE: 'resource'
};

class AlertManager {
  constructor(config) {
    this.config = Object.assign({
      appId: 'default-app',
      thresholds: {
        fp: { warning: 1800, critical: 3000 },
        fcp: { warning: 1800, critical: 3000 },
        lcp: { warning: 2500, critical: 4000 },
        ttfb: { warning: 800, critical: 1800 },
        dom_ready: { warning: 3000, critical: 5000 },
        load_time: { warning: 3000, critical: 5000 },
        fps: { warning: 30, critical: 15 },
        error_count: { warning: 5, critical: 15 },
        jank_count: { warning: 3, critical: 10 }
      },
      onAlert: null,
      maxAlerts: 50,
      debounceTime: 5000
    }, config || {});

    this.alerts = [];
    this._lastAlertTime = {};
    this._alertCount = {};
  }

  checkPerformance(metrics) {
    var alerts = [];
    var metricNames = ['fp', 'fcp', 'lcp', 'ttfb', 'dom_ready', 'load_time'];

    for (var i = 0; i < metricNames.length; i++) {
      var name = metricNames[i];
      var value = metrics[name];

      if (value !== undefined && value !== null) {
        var threshold = this.config.thresholds[name];
        if (threshold) {
          var level = this._getLevel(value, threshold, true);
          if (level !== AlertLevel.INFO) {
            alerts.push({
              type: AlertType.PERFORMANCE,
              level: level,
              metric: name,
              value: Math.round(value),
              threshold: threshold,
              message: this._getPerformanceMessage(name, value, level),
              timestamp: new Date().toISOString()
            });
          }
        }
      }
    }

    return this._processAlerts(alerts);
  }

  checkRenderer(rendererMetrics) {
    var alerts = [];

    if (rendererMetrics.fps !== undefined) {
      var fpsThreshold = this.config.thresholds.fps;
      var fpsLevel = this._getLevel(rendererMetrics.fps, fpsThreshold, false);
      if (fpsLevel !== AlertLevel.INFO) {
        alerts.push({
          type: AlertType.RENDERER,
          level: fpsLevel,
          metric: 'fps',
          value: Math.round(rendererMetrics.fps),
          threshold: fpsThreshold,
          message: '帧率过低: ' + Math.round(rendererMetrics.fps) + ' FPS',
          timestamp: new Date().toISOString()
        });
      }
    }

    if (rendererMetrics.jank_count !== undefined && rendererMetrics.jank_count > 0) {
      var jankThreshold = this.config.thresholds.jank_count;
      var jankLevel = this._getLevel(rendererMetrics.jank_count, jankThreshold, true);
      if (jankLevel !== AlertLevel.INFO) {
        alerts.push({
          type: AlertType.RENDERER,
          level: jankLevel,
          metric: 'jank_count',
          value: rendererMetrics.jank_count,
          threshold: jankThreshold,
          message: '检测到页面卡顿: ' + rendererMetrics.jank_count + ' 次',
          timestamp: new Date().toISOString()
        });
      }
    }

    return this._processAlerts(alerts);
  }

  checkError(errorInfo) {
    var alerts = [];
    var level = AlertLevel.WARNING;

    if (errorInfo.error_type === 'js_error' || errorInfo.error_type === 'promise_rejection') {
      level = AlertLevel.CRITICAL;
    } else if (errorInfo.error_type === 'http_error') {
      level = AlertLevel.WARNING;
    } else if (errorInfo.error_type === 'resource_error') {
      level = AlertLevel.WARNING;
    }

    alerts.push({
      type: AlertType.ERROR,
      level: level,
      metric: 'error',
      value: errorInfo.message.substring(0, 100),
      message: this._getErrorMessage(errorInfo),
      error: errorInfo,
      timestamp: new Date().toISOString()
    });

    return this._processAlerts(alerts);
  }

  checkErrorCount(count, timeWindow) {
    var alerts = [];
    var threshold = this.config.thresholds.error_count;

    if (count >= threshold.critical) {
      alerts.push({
        type: AlertType.ERROR,
        level: AlertLevel.CRITICAL,
        metric: 'error_count',
        value: count,
        threshold: threshold,
        message: '错误频率过高: ' + count + ' 次错误/' + (timeWindow || '窗口'),
        timestamp: new Date().toISOString()
      });
    } else if (count >= threshold.warning) {
      alerts.push({
        type: AlertType.ERROR,
        level: AlertLevel.WARNING,
        metric: 'error_count',
        value: count,
        threshold: threshold,
        message: '错误频率偏高: ' + count + ' 次错误/' + (timeWindow || '窗口'),
        timestamp: new Date().toISOString()
      });
    }

    return this._processAlerts(alerts);
  }

  _getLevel(value, threshold, isHigherWorse) {
    if (isHigherWorse) {
      if (value >= threshold.critical) return AlertLevel.CRITICAL;
      if (value >= threshold.warning) return AlertLevel.WARNING;
    } else {
      if (value <= threshold.critical) return AlertLevel.CRITICAL;
      if (value <= threshold.warning) return AlertLevel.WARNING;
    }
    return AlertLevel.INFO;
  }

  _getPerformanceMessage(metric, value, level) {
    var metricLabels = {
      fp: '首次绘制',
      fcp: '首次内容绘制',
      lcp: '最大内容绘制',
      ttfb: '首字节时间',
      dom_ready: 'DOM就绪时间',
      load_time: '页面加载时间'
    };

    var label = metricLabels[metric] || metric;
    var levelText = level === AlertLevel.CRITICAL ? '严重' : '警告';

    return levelText + ': ' + label + '耗时 ' + Math.round(value) + 'ms';
  }

  _getErrorMessage(errorInfo) {
    var typeLabels = {
      js_error: 'JS运行时错误',
      promise_rejection: 'Promise拒绝',
      resource_error: '资源加载错误',
      http_error: 'HTTP请求错误',
      manual_capture: '手动捕获错误'
    };

    var typeLabel = typeLabels[errorInfo.error_type] || errorInfo.error_type;
    return typeLabel + ': ' + errorInfo.message.substring(0, 80);
  }

  _processAlerts(newAlerts) {
    var processedAlerts = [];
    var now = Date.now();

    for (var i = 0; i < newAlerts.length; i++) {
      var alert = newAlerts[i];
      var key = alert.type + '_' + alert.metric + '_' + alert.level;

      var lastTime = this._lastAlertTime[key] || 0;
      if (now - lastTime >= this.config.debounceTime) {
        this._lastAlertTime[key] = now;

        if (!this._alertCount[key]) {
          this._alertCount[key] = 0;
        }
        this._alertCount[key]++;

        alert.count = this._alertCount[key];
        alert.id = 'alert_' + now + '_' + Math.random().toString(36).substr(2, 9);

        this.alerts.push(alert);
        if (this.alerts.length > this.config.maxAlerts) {
          this.alerts.shift();
        }

        processedAlerts.push(alert);

        if (this.config.onAlert && typeof this.config.onAlert === 'function') {
          try {
            this.config.onAlert(alert);
          } catch (e) {
            console.warn('[AlertManager] Alert callback error:', e);
          }
        }
      }
    }

    return processedAlerts;
  }

  getAlerts(filter) {
    var result = this.alerts.slice();

    if (filter) {
      if (filter.type) {
        result = result.filter(function(a) { return a.type === filter.type; });
      }
      if (filter.level) {
        result = result.filter(function(a) { return a.level === filter.level; });
      }
      if (filter.since) {
        var sinceTime = new Date(filter.since).getTime();
        result = result.filter(function(a) {
          return new Date(a.timestamp).getTime() >= sinceTime;
        });
      }
    }

    return result.sort(function(a, b) {
      return new Date(b.timestamp).getTime() - new Date(a.timestamp).getTime();
    });
  }

  getAlertSummary() {
    var summary = {
      total: this.alerts.length,
      critical: 0,
      warning: 0,
      info: 0,
      byType: {}
    };

    for (var i = 0; i < this.alerts.length; i++) {
      var alert = this.alerts[i];

      if (alert.level === AlertLevel.CRITICAL) summary.critical++;
      else if (alert.level === AlertLevel.WARNING) summary.warning++;
      else summary.info++;

      if (!summary.byType[alert.type]) {
        summary.byType[alert.type] = 0;
      }
      summary.byType[alert.type]++;
    }

    return summary;
  }

  clearAlerts() {
    this.alerts = [];
    this._lastAlertTime = {};
    this._alertCount = {};
  }

  updateThresholds(thresholds) {
    if (thresholds) {
      for (var key in thresholds) {
        if (thresholds.hasOwnProperty(key) && this.config.thresholds[key]) {
          Object.assign(this.config.thresholds[key], thresholds[key]);
        }
      }
    }
  }
}

if (typeof window !== 'undefined') {
  window.AlertManager = AlertManager;
  window.AlertLevel = AlertLevel;
  window.AlertType = AlertType;
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { AlertManager: AlertManager, AlertLevel: AlertLevel, AlertType: AlertType };
}
