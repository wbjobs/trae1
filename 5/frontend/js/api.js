class MonitorAPI {
  constructor(config) {
    this.config = Object.assign({
      baseUrl: 'http://localhost:8000/api',
      timeout: 10000
    }, config || {});
  }

  async _request(endpoint, method, data) {
    var url = this.config.baseUrl + endpoint;
    var options = {
      method: method || 'GET',
      headers: {
        'Content-Type': 'application/json'
      },
      timeout: this.config.timeout
    };

    if (data) {
      options.body = JSON.stringify(data);
    }

    if (typeof fetch === 'undefined') {
      return this._xhrRequest(url, options);
    }

    try {
      var response = await fetch(url, options);

      if (!response.ok) {
        var errorText = await response.text();
        throw new Error('HTTP ' + response.status + ': ' + (errorText || response.statusText));
      }

      return await response.json();
    } catch (error) {
      console.error('[MonitorAPI] Request failed:', endpoint, error.message);
      throw error;
    }
  }

  _xhrRequest(url, options) {
    return new Promise(function(resolve, reject) {
      try {
        var xhr = new XMLHttpRequest();
        xhr.open(options.method, url, true);
        xhr.setRequestHeader('Content-Type', 'application/json');
        xhr.timeout = options.timeout;

        xhr.onload = function() {
          if (xhr.status >= 200 && xhr.status < 300) {
            try {
              resolve(JSON.parse(xhr.responseText));
            } catch (e) {
              resolve(xhr.responseText);
            }
          } else {
            reject(new Error('HTTP ' + xhr.status + ': ' + xhr.statusText));
          }
        };

        xhr.onerror = function() {
          reject(new Error('Network error'));
        };

        xhr.ontimeout = function() {
          reject(new Error('Request timeout'));
        };

        xhr.send(options.body || null);
      } catch (e) {
        reject(e);
      }
    });
  }

  async getPerformanceTrends(appId, startTime, endTime, pageUrl) {
    var data = {
      app_id: appId,
      start_time: startTime,
      end_time: endTime,
      page_url: pageUrl || null
    };

    return this._request('/query/performance/trends', 'POST', data);
  }

  async getPerformanceTrend(appId, metric, startTime, endTime, pageUrl) {
    var data = {
      app_id: appId,
      start_time: startTime,
      end_time: endTime,
      page_url: pageUrl || null
    };

    return this._request('/query/performance/trend/' + metric, 'POST', data);
  }

  async getErrorSummary(appId, startTime, endTime, pageUrl) {
    var data = {
      app_id: appId,
      start_time: startTime,
      end_time: endTime,
      page_url: pageUrl || null
    };

    return this._request('/query/errors/summary', 'POST', data);
  }

  async getErrorDetails(appId, startTime, endTime, errorType, limit, offset) {
    var data = {
      app_id: appId,
      start_time: startTime,
      end_time: endTime
    };

    var endpoint = '/query/errors/details';
    var params = [];

    if (errorType) {
      params.push('error_type=' + encodeURIComponent(errorType));
    }
    params.push('limit=' + (limit || 100));
    params.push('offset=' + (offset || 0));

    if (params.length > 0) {
      endpoint += '?' + params.join('&');
    }

    return this._request(endpoint, 'POST', data);
  }

  async getRendererSummary(appId, startTime, endTime, pageUrl) {
    var data = {
      app_id: appId,
      start_time: startTime,
      end_time: endTime,
      page_url: pageUrl || null
    };

    return this._request('/query/renderer/summary', 'POST', data);
  }

  async getAppStats(appId, startTime, endTime) {
    var data = {
      app_id: appId,
      start_time: startTime,
      end_time: endTime
    };

    return this._request('/query/stats', 'POST', data);
  }
}

if (typeof window !== 'undefined') {
  window.MonitorAPI = MonitorAPI;
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = MonitorAPI;
}
