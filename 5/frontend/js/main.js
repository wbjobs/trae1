class FrontendMonitor {
  constructor(config) {
    this.config = Object.assign({
      appId: 'default-app',
      userId: null,
      reportUrl: 'http://localhost:8000/api/report',
      apiUrl: 'http://localhost:8000/api',
      enablePerformance: true,
      enableErrorTracking: true,
      enableRenderer: true,
      enableAlert: true,
      enableScoring: true,
      sampleRate: 1,
      alertThresholds: null,
      onAlert: null
    }, config || {});

    this.performanceMonitor = null;
    this.errorTracker = null;
    this.rendererMonitor = null;
    this.api = null;
    this.alertManager = null;
    this.scorer = null;
    this.exporter = null;
    this._alertCheckInterval = null;

    this._init();
  }

  _init() {
    if (typeof window === 'undefined') {
      console.warn('[FrontendMonitor] Not running in browser environment');
      return;
    }

    try {
      this.api = new MonitorAPI({
        baseUrl: this.config.apiUrl
      });

      this.exporter = new DataExporter({
        appId: this.config.appId,
        apiUrl: this.config.apiUrl
      });

      if (this.config.enableAlert && typeof AlertManager !== 'undefined') {
        var alertConfig = {
          appId: this.config.appId,
          onAlert: this.config.onAlert || null
        };
        if (this.config.alertThresholds) {
          alertConfig.thresholds = this.config.alertThresholds;
        }
        this.alertManager = new AlertManager(alertConfig);
      }

      if (this.config.enableScoring && typeof PerformanceScorer !== 'undefined') {
        this.scorer = new PerformanceScorer();
      }

      if (this.config.enablePerformance && typeof PerformanceMonitor !== 'undefined') {
        this.performanceMonitor = new PerformanceMonitor({
          appId: this.config.appId,
          userId: this.config.userId,
          reportUrl: this.config.reportUrl,
          sampleRate: this.config.sampleRate,
          onMetricsReady: this._onPerformanceMetrics.bind(this)
        });
      }

      if (this.config.enableErrorTracking && typeof ErrorTracker !== 'undefined') {
        this.errorTracker = new ErrorTracker({
          appId: this.config.appId,
          userId: this.config.userId,
          reportUrl: this.config.reportUrl,
          onError: this._onErrorCaptured.bind(this)
        });
      }

      if (this.config.enableRenderer && typeof RendererMonitor !== 'undefined') {
        this.rendererMonitor = new RendererMonitor({
          appId: this.config.appId,
          userId: this.config.userId,
          reportUrl: this.config.reportUrl,
          autoStart: true,
          onMetricsUpdate: this._onRendererMetrics.bind(this)
        });
      }

      this._startPeriodicAlertCheck();

    } catch (e) {
      console.error('[FrontendMonitor] Initialization error:', e.message);
    }
  }

  _onPerformanceMetrics(metrics) {
    if (this.alertManager && metrics) {
      this.alertManager.checkPerformance(metrics);
    }

    if (this.scorer && metrics) {
      var scoreResult = this.scorer.calculateScore(metrics);
      if (scoreResult && scoreResult.details) {
        console.log('[FrontendMonitor] Performance Score:', scoreResult.overall, scoreResult.grade);
      }
    }
  }

  _onErrorCaptured(errorInfo) {
    if (this.alertManager && errorInfo) {
      this.alertManager.checkError(errorInfo);
    }
  }

  _onRendererMetrics(rendererMetrics) {
    if (this.alertManager && rendererMetrics) {
      this.alertManager.checkRenderer(rendererMetrics);
    }
  }

  _startPeriodicAlertCheck() {
    if (this._alertCheckInterval) {
      clearInterval(this._alertCheckInterval);
    }

    var self = this;
    this._alertCheckInterval = setInterval(function() {
      self._checkErrorRate();
    }, 30000);
  }

  _checkErrorRate() {
    if (!this.errorTracker || !this.alertManager) {
      return;
    }

    var errors = this.errorTracker.getErrors();
    var recentErrors = errors.filter(function(e) {
      var time = new Date(e.timestamp).getTime();
      return Date.now() - time < 30000;
    });

    if (recentErrors.length > 0) {
      this.alertManager.checkErrorCount(recentErrors.length, '30s');
    }
  }

  getMetrics() {
    return {
      performance: this.performanceMonitor ? this.performanceMonitor.getMetrics() : null,
      renderer: this.rendererMonitor ? this.rendererMonitor.getCurrentMetrics() : null,
      errors: this.errorTracker ? this.errorTracker.getErrors() : null
    };
  }

  getScore() {
    if (!this.scorer || !this.performanceMonitor) {
      return null;
    }

    var metrics = this.performanceMonitor.getMetrics();
    if (!metrics || Object.keys(metrics).length === 0) {
      return null;
    }

    return this.scorer.calculateScore(metrics);
  }

  getAlerts(filter) {
    if (!this.alertManager) {
      return [];
    }
    return this.alertManager.getAlerts(filter);
  }

  getAlertSummary() {
    if (!this.alertManager) {
      return null;
    }
    return this.alertManager.getAlertSummary();
  }

  captureError(error, extraInfo) {
    if (this.errorTracker && typeof this.errorTracker.manualCapture === 'function') {
      this.errorTracker.manualCapture(error, extraInfo || {});
    }
  }

  reportRenderer() {
    if (this.rendererMonitor && typeof this.rendererMonitor.manualReport === 'function') {
      this.rendererMonitor.manualReport();
    }
  }

  collectPerformance() {
    if (this.performanceMonitor && typeof this.performanceMonitor.collectNow === 'function') {
      return this.performanceMonitor.collectNow();
    }
    return null;
  }

  exportData(dataType, options) {
    if (!this.exporter) {
      return null;
    }

    options = options || {};
    var format = options.format || 'csv';

    if (dataType === 'performance' && this.performanceMonitor) {
      var metrics = this.performanceMonitor.getMetrics();
      if (format === 'json') {
        return this.exporter.exportToJSON(metrics, options);
      }
      return this.exporter.exportToCSV([metrics], options);
    }

    if (dataType === 'errors' && this.errorTracker) {
      var errors = this.errorTracker.getErrors();
      if (format === 'json') {
        return this.exporter.exportToJSON(errors, options);
      }
      return this.exporter.exportToCSV(errors, options);
    }

    if (dataType === 'renderer' && this.rendererMonitor) {
      var rendererMetrics = this.rendererMonitor.getCurrentMetrics();
      var data = [{
        fps: rendererMetrics.fps,
        long_task_count: rendererMetrics.long_task_count,
        jank_count: rendererMetrics.jank_count,
        memory_used: rendererMetrics.memory_used,
        timestamp: new Date().toISOString()
      }];
      if (format === 'json') {
        return this.exporter.exportToJSON(data, options);
      }
      return this.exporter.exportToCSV(data, options);
    }

    if (dataType === 'alerts' && this.alertManager) {
      var alerts = this.alertManager.getAlerts();
      if (format === 'json') {
        return this.exporter.exportToJSON(alerts, options);
      }
      return this.exporter.exportToCSV(alerts, options);
    }

    return null;
  }

  exportScoreReport() {
    if (!this.exporter || !this.scorer || !this.performanceMonitor) {
      return null;
    }

    var metrics = this.performanceMonitor.getMetrics();
    if (!metrics || Object.keys(metrics).length === 0) {
      return null;
    }

    var scoreResult = this.scorer.calculateScore(metrics);
    return this.exporter.exportPerformanceReport(scoreResult);
  }

  filterData(data, filters) {
    if (!this.exporter) {
      return data;
    }
    return this.exporter.filterData(data, filters);
  }

  aggregateData(data, options) {
    if (!this.exporter) {
      return [];
    }
    return this.exporter.aggregateData(data, options);
  }

  async queryPerformanceTrends(startTime, endTime, pageUrl) {
    if (!this.api) return null;
    return this.api.getPerformanceTrends(
      this.config.appId,
      startTime,
      endTime,
      pageUrl || null
    );
  }

  async queryErrorSummary(startTime, endTime, pageUrl) {
    if (!this.api) return null;
    return this.api.getErrorSummary(
      this.config.appId,
      startTime,
      endTime,
      pageUrl || null
    );
  }

  async queryErrorDetails(startTime, endTime, errorType, limit, offset) {
    if (!this.api) return null;
    return this.api.getErrorDetails(
      this.config.appId,
      startTime,
      endTime,
      errorType || null,
      limit || 100,
      offset || 0
    );
  }

  async queryRendererSummary(startTime, endTime, pageUrl) {
    if (!this.api) return null;
    return this.api.getRendererSummary(
      this.config.appId,
      startTime,
      endTime,
      pageUrl || null
    );
  }

  async queryAppStats(startTime, endTime) {
    if (!this.api) return null;
    return this.api.getAppStats(
      this.config.appId,
      startTime,
      endTime
    );
  }

  updateAlertThresholds(thresholds) {
    if (this.alertManager) {
      this.alertManager.updateThresholds(thresholds);
    }
  }

  destroy() {
    if (this._alertCheckInterval) {
      clearInterval(this._alertCheckInterval);
      this._alertCheckInterval = null;
    }

    if (this.performanceMonitor && typeof this.performanceMonitor.destroy === 'function') {
      this.performanceMonitor.destroy();
    }
    if (this.errorTracker && typeof this.errorTracker.destroy === 'function') {
      this.errorTracker.destroy();
    }
    if (this.rendererMonitor && typeof this.rendererMonitor.destroy === 'function') {
      this.rendererMonitor.destroy();
    }
    if (this.alertManager) {
      this.alertManager.clearAlerts();
    }
    if (this.scorer) {
      this.scorer.clearHistory();
    }
  }
}

if (typeof window !== 'undefined') {
  window.FrontendMonitor = FrontendMonitor;
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = FrontendMonitor;
}
