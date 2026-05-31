class DataExporter {
  constructor(config) {
    this.config = Object.assign({
      appId: 'default-app',
      apiUrl: 'http://localhost:8000/api'
    }, config || {});
  }

  exportToCSV(data, options) {
    options = options || {};
    var filename = options.filename || 'export_' + Date.now() + '.csv';
    var columns = options.columns || this._getColumns(data);

    if (!data || data.length === 0) {
      console.warn('[DataExporter] No data to export');
      return null;
    }

    var csvContent = this._generateCSV(data, columns);
    this._downloadFile(csvContent, filename, 'text/csv;charset=utf-8;');

    return {
      filename: filename,
      rows: data.length,
      columns: columns.length
    };
  }

  exportToJSON(data, options) {
    options = options || {};
    var filename = options.filename || 'export_' + Date.now() + '.json';
    var pretty = options.pretty !== false;

    if (!data) {
      console.warn('[DataExporter] No data to export');
      return null;
    }

    var jsonContent = pretty ? JSON.stringify(data, null, 2) : JSON.stringify(data);
    this._downloadFile(jsonContent, filename, 'application/json');

    return {
      filename: filename,
      size: jsonContent.length
    };
  }

  exportPerformanceReport(reportData, options) {
    options = options || {};
    var filename = options.filename || 'performance_report_' + Date.now() + '.html';

    var htmlContent = this._generatePerformanceReport(reportData);
    this._downloadFile(htmlContent, filename, 'text/html;charset=utf-8;');

    return {
      filename: filename,
      data: reportData
    };
  }

  async exportHistoricalData(params, options) {
    options = options || {};
    var format = options.format || 'csv';
    var dataType = options.dataType || 'performance';

    try {
      var response = await fetch(this.config.apiUrl + '/query/' + dataType + '/export', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(params)
      });

      if (!response.ok) {
        throw new Error('HTTP ' + response.status);
      }

      var data = await response.json();

      if (format === 'csv') {
        return this.exportToCSV(data, options);
      } else {
        return this.exportToJSON(data, options);
      }
    } catch (error) {
      console.error('[DataExporter] Export failed:', error);
      throw error;
    }
  }

  filterData(data, filters) {
    if (!data || !filters) return data;

    return data.filter(function(item) {
      var match = true;

      if (filters.startTime) {
        var startTime = new Date(filters.startTime).getTime();
        var itemTime = new Date(item.timestamp || item.time || item._time).getTime();
        match = match && itemTime >= startTime;
      }

      if (filters.endTime) {
        var endTime = new Date(filters.endTime).getTime();
        var itemTime2 = new Date(item.timestamp || item.time || item._time).getTime();
        match = match && itemTime2 <= endTime;
      }

      if (filters.level) {
        match = match && item.level === filters.level;
      }

      if (filters.type) {
        match = match && item.type === filters.type;
      }

      if (filters.error_type) {
        match = match && item.error_type === filters.error_type;
      }

      if (filters.minScore !== undefined) {
        match = match && item.score >= filters.minScore;
      }

      if (filters.maxScore !== undefined) {
        match = match && item.score <= filters.maxScore;
      }

      return match;
    });
  }

  aggregateData(data, options) {
    options = options || {};
    var groupBy = options.groupBy || 'hour';
    var metric = options.metric || 'value';
    var aggregations = options.aggregations || ['count', 'avg', 'min', 'max'];

    var groups = {};

    for (var i = 0; i < data.length; i++) {
      var item = data[i];
      var time = new Date(item.timestamp || item.time || item._time);
      var key = this._getTimeKey(time, groupBy);

      if (!groups[key]) {
        groups[key] = { values: [], count: 0 };
      }

      var value = item[metric] !== undefined ? item[metric] : item.value;
      if (value !== undefined && !isNaN(value)) {
        groups[key].values.push(parseFloat(value));
      }
      groups[key].count++;
    }

    var result = [];
    for (var key in groups) {
      if (groups.hasOwnProperty(key)) {
        var group = groups[key];
        var entry = { time: key, count: group.count };

        if (group.values.length > 0) {
          if (aggregations.indexOf('avg') !== -1) {
            entry.avg = Math.round(group.values.reduce(function(a, b) { return a + b; }, 0) / group.values.length * 100) / 100;
          }
          if (aggregations.indexOf('min') !== -1) {
            entry.min = Math.min.apply(null, group.values);
          }
          if (aggregations.indexOf('max') !== -1) {
            entry.max = Math.max.apply(null, group.values);
          }
          if (aggregations.indexOf('sum') !== -1) {
            entry.sum = Math.round(group.values.reduce(function(a, b) { return a + b; }, 0) * 100) / 100;
          }
        }

        result.push(entry);
      }
    }

    return result.sort(function(a, b) {
      return new Date(a.time).getTime() - new Date(b.time).getTime();
    });
  }

  _getColumns(data) {
    var columns = new Set();
    for (var i = 0; i < data.length; i++) {
      for (var key in data[i]) {
        if (data[i].hasOwnProperty(key) && typeof data[i][key] !== 'object') {
          columns.add(key);
        }
      }
    }
    return Array.from(columns);
  }

  _generateCSV(data, columns) {
    var header = columns.map(function(col) {
      return this._escapeCSV(col);
    }.bind(this)).join(',');

    var rows = data.map(function(row) {
      return columns.map(function(col) {
        var value = row[col];
        if (value === null || value === undefined) {
          return '';
        }
        return this._escapeCSV(String(value));
      }.bind(this)).join(',');
    }.bind(this));

    return '\uFEFF' + [header].concat(rows).join('\n');
  }

  _escapeCSV(value) {
    if (value === null || value === undefined) return '';
    value = String(value);
    if (value.indexOf(',') !== -1 || value.indexOf('"') !== -1 || value.indexOf('\n') !== -1) {
      value = '"' + value.replace(/"/g, '""') + '"';
    }
    return value;
  }

  _getTimeKey(date, groupBy) {
    var d = new Date(date);
    switch (groupBy) {
      case 'minute':
        return d.toISOString().substring(0, 16) + ':00Z';
      case 'hour':
        return d.toISOString().substring(0, 13) + ':00:00Z';
      case 'day':
        return d.toISOString().substring(0, 10) + 'T00:00:00Z';
      case 'week':
        var weekStart = new Date(d);
        weekStart.setDate(d.getDate() - d.getDay());
        return weekStart.toISOString().substring(0, 10) + 'T00:00:00Z';
      case 'month':
        return d.toISOString().substring(0, 7) + '-01T00:00:00Z';
      default:
        return d.toISOString();
    }
  }

  _generatePerformanceReport(report) {
    return '<!DOCTYPE html>\n' +
      '<html>\n<head>\n<meta charset="UTF-8">\n' +
      '<title>性能报告</title>\n' +
      '<style>\n' +
      'body { font-family: Arial, sans-serif; margin: 40px; }\n' +
      'h1 { color: #333; }\n' +
      '.score { font-size: 48px; font-weight: bold; }\n' +
      '.grade-A { color: #4caf50; }\n' +
      '.grade-B { color: #8bc34a; }\n' +
      '.grade-C { color: #ffeb3b; }\n' +
      '.grade-D { color: #ff9800; }\n' +
      '.grade-F { color: #f44336; }\n' +
      'table { border-collapse: collapse; width: 100%; margin-top: 20px; }\n' +
      'th, td { border: 1px solid #ddd; padding: 12px; text-align: left; }\n' +
      'th { background-color: #f5f5f5; }\n' +
      '.suggestion { background: #fff3e0; padding: 12px; margin: 8px 0; border-radius: 4px; }\n' +
      '</style>\n</head>\n<body>\n' +
      '<h1>页面性能报告</h1>\n' +
      '<p>生成时间: ' + new Date().toLocaleString() + '</p>\n' +
      '<div>综合评分: <span class="score grade-' + report.grade + '">' + report.overall + '</span> (' + PerformanceScorer.getGradeLabel(report.grade) + ')</div>\n' +
      (report.details ? '<p>' + report.details.summary + '</p>' : '') +
      '<h2>各项指标</h2>\n' +
      '<table>\n<tr><th>指标</th><th>数值</th><th>评分</th><th>等级</th></tr>\n' +
      Object.keys(report.metrics || {}).map(function(key) {
        var m = report.metrics[key];
        return '<tr><td>' + key + '</td><td>' + (m.value || '-') + ' ms</td><td>' + m.score.toFixed(2) + '</td><td>' + m.grade + '</td></tr>';
      }).join('\n') +
      '\n</table>\n' +
      (report.details && report.details.suggestions && report.details.suggestions.length > 0 ?
        '<h2>优化建议</h2>\n' +
        report.details.suggestions.map(function(s) { return '<div class="suggestion">' + s + '</div>'; }).join('\n') : '') +
      '</body>\n</html>';
  }

  _downloadFile(content, filename, mimeType) {
    var blob = new Blob([content], { type: mimeType });
    var url = URL.createObjectURL(blob);
    var link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
  }
}

if (typeof window !== 'undefined') {
  window.DataExporter = DataExporter;
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = DataExporter;
}
