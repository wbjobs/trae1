class ErrorTracker {
  constructor(config) {
    this.config = Object.assign({
      appId: 'default-app',
      userId: null,
      reportUrl: 'http://localhost:8000/api/report',
      maxErrors: 100,
      ignoreErrors: [],
      captureHttp: true,
      captureResource: true,
      maxRetries: 3
    }, config || {});

    this.config.sessionId = this._generateSessionId();
    this.errorQueue = [];
    this._handlers = {};
    this._initialized = false;
    this._init();
  }

  _generateSessionId() {
    return 'session_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
  }

  _init() {
    if (typeof window === 'undefined') {
      console.warn('[ErrorTracker] window object not available');
      return;
    }

    try {
      this._initErrorHandler();
      this._initRejectionHandler();

      if (this.config.captureResource) {
        this._initResourceErrorHandler();
      }

      if (this.config.captureHttp) {
        this._initHttpErrorHandler();
      }

      this._initialized = true;
    } catch (e) {
      console.warn('[ErrorTracker] Initialization failed:', e.message);
    }
  }

  _initErrorHandler() {
    try {
      this._handlers.error = (event) => {
        if (event.error || event.message) {
          const errorInfo = {
            type: 'js',
            error_type: 'js_error',
            message: event.message || 'Unknown error',
            filename: event.filename || null,
            lineno: event.lineno || null,
            colno: event.colno || null,
            stack: event.error ? event.error.stack : null
          };

          if (!this._shouldIgnore(errorInfo)) {
            this._captureError(errorInfo);
          }
        }
      };

      window.addEventListener('error', this._handlers.error, true);
    } catch (e) {
      console.warn('[ErrorTracker] Error handler init failed:', e.message);
    }
  }

  _initRejectionHandler() {
    try {
      this._handlers.rejection = (event) => {
        let reason = event.reason;
        let message = 'Unhandled Promise Rejection';
        let stack = null;

        if (reason instanceof Error) {
          message = reason.message;
          stack = reason.stack;
        } else if (typeof reason === 'string') {
          message = reason;
        } else if (typeof reason === 'object' && reason !== null) {
          try {
            message = JSON.stringify(reason);
          } catch (e) {
            message = String(reason);
          }
        }

        const errorInfo = {
          type: 'js',
          error_type: 'promise_rejection',
          message: message,
          stack: stack,
          filename: null,
          lineno: null,
          colno: null
        };

        if (!this._shouldIgnore(errorInfo)) {
          this._captureError(errorInfo);
        }
      };

      window.addEventListener('unhandledrejection', this._handlers.rejection);
    } catch (e) {
      console.warn('[ErrorTracker] Rejection handler init failed:', e.message);
    }
  }

  _initResourceErrorHandler() {
    try {
      this._handlers.resource = (event) => {
        if (event.target && event.target.tagName) {
          const tagName = event.target.tagName.toLowerCase();
          const isResource = ['script', 'link', 'img', 'video', 'audio', 'source'].includes(tagName);

          if (isResource) {
            const src = event.target.src || event.target.href || 'Unknown';
            const errorInfo = {
              type: 'resource',
              error_type: 'resource_error',
              message: 'Failed to load resource: ' + src,
              filename: src,
              lineno: null,
              colno: null,
              stack: null
            };

            if (!this._shouldIgnore(errorInfo)) {
              this._captureError(errorInfo);
            }
          }
        }
      };

      window.addEventListener('error', this._handlers.resource, true);
    } catch (e) {
      console.warn('[ErrorTracker] Resource handler init failed:', e.message);
    }
  }

  _initHttpErrorHandler() {
    if (typeof fetch === 'undefined' && typeof XMLHttpRequest === 'undefined') {
      return;
    }

    if (typeof fetch !== 'undefined') {
      const tracker = this;
      const originalFetch = window.fetch;

      window.fetch = function() {
        var args = Array.prototype.slice.call(arguments);
        var url = args[0];

        return originalFetch.apply(window, args)
          .then(function(response) {
            if (!response.ok) {
              tracker._captureHttpError(url, response.status, response.statusText);
            }
            return response;
          })
          .catch(function(error) {
            tracker._captureHttpError(url, 0, error.message);
            throw error;
          });
      };
    }

    if (typeof XMLHttpRequest !== 'undefined') {
      var tracker = this;
      var originalOpen = XMLHttpRequest.prototype.open;
      var originalSend = XMLHttpRequest.prototype.send;

      XMLHttpRequest.prototype.open = function(method, url) {
        this._trackMethod = method;
        this._trackUrl = url;
        return originalOpen.apply(this, arguments);
      };

      XMLHttpRequest.prototype.send = function() {
        var self = this;

        this.addEventListener('error', function() {
          tracker._captureHttpError(self._trackUrl, 0, 'Network Error');
        });

        this.addEventListener('load', function() {
          if (self.status >= 400) {
            tracker._captureHttpError(self._trackUrl, self.status, self.statusText);
          }
        });

        return originalSend.apply(this, arguments);
      };
    }
  }

  _shouldIgnore(errorInfo) {
    if (!this.config.ignoreErrors || this.config.ignoreErrors.length === 0) {
      return false;
    }

    return this.config.ignoreErrors.some(function(pattern) {
      if (pattern instanceof RegExp) {
        return pattern.test(errorInfo.message);
      }
      return errorInfo.message.indexOf(pattern) !== -1;
    });
  }

  _captureError(errorInfo) {
    if (!this._initialized) {
      return;
    }

    if (this.errorQueue.length >= this.config.maxErrors) {
      this.errorQueue.shift();
    }

    var reportData = {
      app_id: this.config.appId,
      user_id: this.config.userId,
      session_id: this.config.sessionId,
      page_url: typeof window !== 'undefined' ? window.location.href : '',
      user_agent: typeof navigator !== 'undefined' ? navigator.userAgent : '',
      timestamp: new Date().toISOString(),
      error: {
        type: errorInfo.type,
        message: String(errorInfo.message || '').substring(0, 1000),
        stack: errorInfo.stack ? String(errorInfo.stack).substring(0, 5000) : null,
        filename: errorInfo.filename || null,
        lineno: errorInfo.lineno || null,
        colno: errorInfo.colno || null,
        error_type: errorInfo.error_type || 'unknown'
      }
    };

    this.errorQueue.push(reportData);
    this._reportError(reportData);
  }

  _captureHttpError(url, status, statusText) {
    var errorInfo = {
      type: 'http',
      error_type: 'http_error',
      message: 'HTTP ' + status + ': ' + url + ' - ' + (statusText || ''),
      filename: url,
      lineno: null,
      colno: null,
      stack: null
    };

    if (!this._shouldIgnore(errorInfo)) {
      this._captureError(errorInfo);
    }
  }

  _reportError(reportData, retryCount) {
    retryCount = retryCount || 0;
    var url = this.config.reportUrl + '/error';

    try {
      if (typeof navigator !== 'undefined' && navigator.sendBeacon) {
        var blob = new Blob([JSON.stringify(reportData)], { type: 'application/json' });
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
        body: JSON.stringify(reportData),
        keepalive: true
      }).then(function(response) {
        if (!response.ok) {
          throw new Error('HTTP ' + response.status);
        }
      }).catch(function(err) {
        console.warn('[ErrorTracker] Report failed:', err.message);
        if (retryCount < 3) {
          setTimeout(function() {
            this._reportError(reportData, retryCount + 1);
          }.bind(this), 1000 * (retryCount + 1));
        }
      }.bind(this));
    } else if (typeof XMLHttpRequest !== 'undefined') {
      try {
        var xhr = new XMLHttpRequest();
        xhr.open('POST', url, true);
        xhr.setRequestHeader('Content-Type', 'application/json');
        xhr.send(JSON.stringify(reportData));
      } catch (e) {
        console.warn('[ErrorTracker] XHR report failed:', e.message);
      }
    }
  }

  getErrors() {
    return this.errorQueue.slice();
  }

  clearErrors() {
    this.errorQueue = [];
  }

  manualCapture(error, extraInfo) {
    extraInfo = extraInfo || {};

    var errorInfo = Object.assign({
      type: 'manual',
      error_type: 'manual_capture',
      message: (error && error.message) ? error.message : String(error),
      stack: (error && error.stack) ? error.stack : null,
      filename: null,
      lineno: null,
      colno: null
    }, extraInfo);

    this._captureError(errorInfo);
  }

  destroy() {
    if (this._handlers.error) {
      window.removeEventListener('error', this._handlers.error, true);
    }
    if (this._handlers.rejection) {
      window.removeEventListener('unhandledrejection', this._handlers.rejection);
    }
    if (this._handlers.resource) {
      window.removeEventListener('error', this._handlers.resource, true);
    }

    this.errorQueue = [];
    this._initialized = false;
  }
}

if (typeof window !== 'undefined') {
  window.ErrorTracker = ErrorTracker;
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = ErrorTracker;
}
