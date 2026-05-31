let monitor = null;
let currentTab = 'performance';

function startMonitor() {
  const appId = document.getElementById('appId').value || 'demo-app';
  const userId = document.getElementById('userId').value || null;
  const backendUrl = document.getElementById('backendUrl').value || 'http://localhost:8000';
  const sampleRate = parseFloat(document.getElementById('sampleRate').value) || 1;

  if (monitor) {
    monitor.destroy();
  }

  monitor = new FrontendMonitor({
    appId: appId,
    userId: userId,
    reportUrl: backendUrl + '/api/report',
    apiUrl: backendUrl + '/api',
    sampleRate: sampleRate
  });

  showNotification('✅ 监控已启动');

  setTimeout(updateDashboard, 2000);
}

function stopMonitor() {
  if (monitor) {
    monitor.destroy();
    monitor = null;
    showNotification('⏹️ 监控已停止');
  }
}

function showNotification(message) {
  const notification = document.createElement('div');
  notification.style.cssText = `
    position: fixed;
    top: 20px;
    right: 20px;
    background: #4caf50;
    color: white;
    padding: 12px 24px;
    border-radius: 8px;
    box-shadow: 0 4px 12px rgba(0,0,0,0.15);
    z-index: 1000;
    animation: slideIn 0.3s ease-out;
  `;
  notification.textContent = message;
  document.body.appendChild(notification);

  setTimeout(() => {
    notification.style.animation = 'slideOut 0.3s ease-in';
    setTimeout(() => notification.remove(), 300);
  }, 2000);
}

function updateDashboard() {
  if (!monitor) return;

  const metrics = monitor.getMetrics();
  updatePerformanceMetrics(metrics.performance);
  updateRendererMetrics(metrics.renderer);
  updateErrorList(metrics.errors);
  updateScore();
  updateAlerts();

  setTimeout(updateDashboard, 3000);
}

function updatePerformanceMetrics(perf) {
  const container = document.getElementById('performanceMetrics');

  if (!perf || Object.keys(perf).length === 0) {
    container.innerHTML = `
      <div class="empty-state">
        <div class="empty-state-icon">📊</div>
        <p>等待采集性能数据...</p>
      </div>
    `;
    return;
  }

  const metricConfigs = [
    { key: 'fp', label: 'FP 首次绘制', good: 1800, warn: 3000 },
    { key: 'fcp', label: 'FCP 首次内容绘制', good: 1800, warn: 3000 },
    { key: 'lcp', label: 'LCP 最大内容绘制', good: 2500, warn: 4000 },
    { key: 'ttfb', label: 'TTFB 首字节时间', good: 800, warn: 1800 },
    { key: 'domReady', label: 'DOM Ready', good: 3000, warn: 5000 },
    { key: 'loadTime', label: '页面加载完成', good: 3000, warn: 5000 }
  ];

  let html = '';
  metricConfigs.forEach(config => {
    const value = perf[config.key];
    if (value !== undefined && value !== null) {
      const status = value < config.good ? 'good' : value < config.warn ? 'warning' : 'bad';
      const displayValue = Math.round(value) + ' ms';
      html += `
        <div class="metric-row">
          <span class="metric-label">${config.label}</span>
          <span class="metric-value ${status}">${displayValue}</span>
        </div>
      `;
    }
  });

  if (!html) {
    html = `
      <div class="empty-state">
        <div class="empty-state-icon">📊</div>
        <p>等待采集性能数据...</p>
      </div>
    `;
  }

  container.innerHTML = html;
}

function updateRendererMetrics(renderer) {
  const container = document.getElementById('rendererMetrics');

  if (!renderer) {
    container.innerHTML = `
      <div class="empty-state">
        <div class="empty-state-icon">🎨</div>
        <p>等待渲染数据...</p>
      </div>
    `;
    return;
  }

  const fpsStatus = renderer.fps >= 50 ? 'good' : renderer.fps >= 30 ? 'warning' : 'bad';
  const jankStatus = renderer.jank_count === 0 ? 'good' : renderer.jank_count < 5 ? 'warning' : 'bad';

  container.innerHTML = `
    <div class="metric-row">
      <span class="metric-label">帧率 (FPS)</span>
      <span class="metric-value ${fpsStatus}">${renderer.fps}</span>
    </div>
    <div class="metric-row">
      <span class="metric-label">内存使用</span>
      <span class="metric-value">${renderer.memory_used ? renderer.memory_used + ' MB' : 'N/A'}</span>
    </div>
    <div class="metric-row">
      <span class="metric-label">长任务数量</span>
      <span class="metric-value">${renderer.long_task_count}</span>
    </div>
    <div class="metric-row">
      <span class="metric-label">卡顿次数</span>
      <span class="metric-value ${jankStatus}">${renderer.jank_count}</span>
    </div>
  `;
}

function updateErrorList(errors) {
  const container = document.getElementById('errorList');

  if (!errors || errors.length === 0) {
    container.innerHTML = `
      <div class="empty-state">
        <div class="empty-state-icon">✅</div>
        <p>暂无异常捕获</p>
      </div>
    `;
    return;
  }

  const recentErrors = errors.slice(-5).reverse();

  let html = '<div class="error-list">';
  recentErrors.forEach(error => {
    const typeClass = 'error-type-' + (error.error?.error_type?.split('_')[0] || 'js');
    const errorType = error.error?.error_type || 'unknown';
    const message = error.error?.message?.substring(0, 100) || 'Unknown error';
    const time = new Date(error.timestamp).toLocaleTimeString();

    html += `
      <div class="error-item">
        <span class="error-type ${typeClass}">${errorType}</span>
        <div class="error-message">${message}</div>
        <div class="error-time">${time}</div>
      </div>
    `;
  });
  html += '</div>';

  container.innerHTML = html;
}

function updateScore() {
  const scoreResult = monitor.getScore();

  if (!scoreResult) {
    return;
  }

  const scoreValue = document.getElementById('overallScore');
  const scoreGrade = document.getElementById('overallGrade');
  const scoreTrend = document.getElementById('scoreTrend');
  const scoreDetails = document.getElementById('scoreDetails');

  scoreValue.textContent = scoreResult.overall;
  scoreValue.className = 'score-value grade-' + scoreResult.grade;

  scoreGrade.textContent = PerformanceScorer.getGradeLabel(scoreResult.grade);
  scoreGrade.className = 'score-grade grade-' + scoreResult.grade;

  if (scoreResult.details) {
    const trend = monitor.scorer.getScoreTrend();
    let trendText = '';
    if (trend.trend === 'improving') {
      trendText = '📈 改善中 (+' + trend.change + ')';
    } else if (trend.trend === 'declining') {
      trendText = '📉 下降中 (' + trend.change + ')';
    } else if (trend.trend === 'stable') {
      trendText = '➡️ 稳定';
    }
    scoreTrend.textContent = trendText;

    let detailsHtml = '<div style="padding: 16px;">';
    detailsHtml += '<p style="margin-bottom: 12px; font-size: 14px;">' + scoreResult.details.summary + '</p>';

    if (scoreResult.details.strengths && scoreResult.details.strengths.length > 0) {
      detailsHtml += '<div style="margin-bottom: 8px;">';
      detailsHtml += '<strong style="color: #4caf50;">✅ 优势指标:</strong> ';
      detailsHtml += scoreResult.details.strengths.join(', ');
      detailsHtml += '</div>';
    }

    if (scoreResult.details.weaknesses && scoreResult.details.weaknesses.length > 0) {
      detailsHtml += '<div style="margin-bottom: 8px;">';
      detailsHtml += '<strong style="color: #f44336;">⚠️ 待优化:</strong> ';
      detailsHtml += scoreResult.details.weaknesses.join(', ');
      detailsHtml += '</div>';
    }

    if (scoreResult.details.suggestions && scoreResult.details.suggestions.length > 0) {
      detailsHtml += '<div style="margin-top: 12px;">';
      detailsHtml += '<strong>💡 优化建议:</strong>';
      detailsHtml += '<ul style="margin-top: 8px; padding-left: 20px; font-size: 13px; color: #666;">';
      scoreResult.details.suggestions.forEach(s => {
        detailsHtml += '<li style="margin-bottom: 4px;">' + s + '</li>';
      });
      detailsHtml += '</ul>';
      detailsHtml += '</div>';
    }

    detailsHtml += '</div>';
    scoreDetails.innerHTML = detailsHtml;
  }
}

function updateAlerts() {
  const alertSummary = monitor.getAlertSummary();
  const alerts = monitor.getAlerts();

  const summaryContainer = document.getElementById('alertSummary');
  const listContainer = document.getElementById('alertList');

  if (alertSummary && alertSummary.total > 0) {
    summaryContainer.innerHTML = `
      <span style="color: #f44336;">严重: ${alertSummary.critical}</span> |
      <span style="color: #ff9800;">警告: ${alertSummary.warning}</span> |
      总计: ${alertSummary.total}
    `;
  } else {
    summaryContainer.innerHTML = '<span style="color: #4caf50;">✅ 无告警</span>';
  }

  if (!alerts || alerts.length === 0) {
    listContainer.innerHTML = `
      <div class="empty-state">
        <div class="empty-state-icon">✅</div>
        <p>暂无告警</p>
      </div>
    `;
    return;
  }

  const recentAlerts = alerts.slice(0, 10);

  let html = '';
  recentAlerts.forEach(alert => {
    const levelClass = 'alert-level-' + alert.level;
    const levelIcon = alert.level === 'critical' ? '🔴' : alert.level === 'warning' ? '🟡' : '🔵';
    const time = new Date(alert.timestamp).toLocaleTimeString();

    html += `
      <div class="alert-item ${levelClass}">
        <span class="alert-icon">${levelIcon}</span>
        <div class="alert-content">
          <div class="alert-message">${alert.message}</div>
          <div class="alert-meta">${alert.type} | ${time}</div>
        </div>
      </div>
    `;
  });

  listContainer.innerHTML = html;
}

function exportData() {
  if (!monitor) {
    showNotification('⚠️ 请先启动监控');
    return;
  }

  const dataType = document.getElementById('exportType').value;
  const format = document.getElementById('exportFormat').value;

  const result = monitor.exportData(dataType, { format: format });

  if (result) {
    showNotification('📤 数据已导出: ' + result.filename);
  } else {
    showNotification('⚠️ 导出失败，请检查数据');
  }
}

function exportScoreReport() {
  if (!monitor) {
    showNotification('⚠️ 请先启动监控');
    return;
  }

  const result = monitor.exportScoreReport();

  if (result) {
    showNotification('📤 评分报告已导出');
  } else {
    showNotification('⚠️ 暂无评分数据');
  }
}

function clearAlerts() {
  if (monitor && monitor.alertManager) {
    monitor.alertManager.clearAlerts();
    updateAlerts();
    showNotification('🗑️ 告警已清除');
  }
}

function switchQueryTab(tab) {
  currentTab = tab;
  document.querySelectorAll('.query-tab').forEach(t => {
    t.classList.toggle('active', t.dataset.tab === tab);
  });
  document.getElementById('queryResult').innerHTML = `
    <div class="empty-state">
      <div class="empty-state-icon">🔍</div>
      <p>点击"查询数据"按钮获取结果</p>
    </div>
  `;
}

function queryData() {
  if (!monitor) {
    showNotification('⚠️ 请先启动监控');
    return;
  }

  const startTime = document.getElementById('startTime').value;
  const endTime = document.getElementById('endTime').value;

  if (!startTime || !endTime) {
    showNotification('⚠️ 请选择时间范围');
    return;
  }

  const startIso = new Date(startTime).toISOString();
  const endIso = new Date(endTime).toISOString();
  const pageUrl = document.getElementById('filterPageUrl').value || null;

  switch (currentTab) {
    case 'performance':
      queryPerformance(startIso, endIso, pageUrl);
      break;
    case 'errors':
      queryErrors(startIso, endIso, pageUrl);
      break;
    case 'renderer':
      queryRenderer(startIso, endIso, pageUrl);
      break;
    case 'stats':
      queryStats(startIso, endIso);
      break;
  }
}

async function queryPerformance(startTime, endTime, pageUrl) {
  const container = document.getElementById('queryResult');
  container.innerHTML = '<div class="empty-state"><p>⏳ 正在查询...</p></div>';

  try {
    const result = await monitor.queryPerformanceTrends(startTime, endTime, pageUrl);
    const data = result.data;

    if (!data || Object.keys(data).length === 0) {
      container.innerHTML = `
        <div class="empty-state">
          <div class="empty-state-icon">📭</div>
          <p>暂无性能数据</p>
        </div>
      `;
      return;
    }

    let html = '<div class="result-summary">';
    const metricLabels = {
      fp: 'FP', fcp: 'FCP', lcp: 'LCP', ttfb: 'TTFB',
      dom_ready: 'DOM Ready', load_time: 'Load Time'
    };

    Object.keys(data).forEach(key => {
      const trend = data[key];
      html += `
        <div class="result-item">
          <div class="label">${metricLabels[key] || key} 平均值</div>
          <div class="value">${trend.avg} ms</div>
        </div>
        <div class="result-item">
          <div class="label">${metricLabels[key] || key} P95</div>
          <div class="value">${trend.p95} ms</div>
        </div>
      `;
    });
    html += '</div>';

    html += '<table class="result-table"><thead><tr>';
    html += '<th>指标</th><th>平均值</th><th>最小值</th><th>最大值</th><th>P50</th><th>P95</th><th>数据点</th>';
    html += '</tr></thead><tbody>';

    Object.keys(data).forEach(key => {
      const trend = data[key];
      html += `
        <tr>
          <td>${metricLabels[key] || key}</td>
          <td>${trend.avg} ms</td>
          <td>${trend.min} ms</td>
          <td>${trend.max} ms</td>
          <td>${trend.p50} ms</td>
          <td>${trend.p95} ms</td>
          <td>${trend.points.length}</td>
        </tr>
      `;
    });

    html += '</tbody></table>';

    container.innerHTML = html;
  } catch (error) {
    container.innerHTML = `
      <div class="empty-state">
        <div class="empty-state-icon">❌</div>
        <p>查询失败: ${error.message}</p>
      </div>
    `;
  }
}

async function queryErrors(startTime, endTime, pageUrl) {
  const container = document.getElementById('queryResult');
  container.innerHTML = '<div class="empty-state"><p>⏳ 正在查询...</p></div>';

  try {
    const result = await monitor.queryErrorSummary(startTime, endTime, pageUrl);
    const summary = result.data;

    if (!summary || summary.length === 0) {
      container.innerHTML = `
        <div class="empty-state">
          <div class="empty-state-icon">✅</div>
          <p>时间范围内无错误记录</p>
        </div>
      `;
      return;
    }

    let html = '<div class="result-summary">';
    const totalErrors = summary.reduce((sum, item) => sum + item.count, 0);
    html += `
      <div class="result-item">
        <div class="label">总错误数</div>
        <div class="value">${totalErrors}</div>
      </div>
      <div class="result-item">
        <div class="label">错误类型</div>
        <div class="value">${summary.length}</div>
      </div>
    `;
    html += '</div>';

    html += '<table class="result-table"><thead><tr>';
    html += '<th>错误类型</th><th>出现次数</th><th>最近出现</th><th>示例消息</th>';
    html += '</tr></thead><tbody>';

    summary.forEach(item => {
      const lastTime = new Date(item.last_occurrence).toLocaleString();
      const messages = item.messages.slice(0, 3).join('; ');
      html += `
        <tr>
          <td>${item.error_type}</td>
          <td>${item.count}</td>
          <td>${lastTime}</td>
          <td>${messages.substring(0, 100)}...</td>
        </tr>
      `;
    });

    html += '</tbody></table>';

    try {
      const detailResult = await monitor.queryErrorDetails(startTime, endTime, null, 10, 0);
      const details = detailResult.data;

      if (details && details.length > 0) {
        html += '<h4 style="margin-top:20px;margin-bottom:10px;">最近错误详情</h4>';
        html += '<table class="result-table"><thead><tr>';
        html += '<th>时间</th><th>类型</th><th>消息</th><th>页面</th>';
        html += '</tr></thead><tbody>';

        details.slice(0, 10).forEach(detail => {
          const time = new Date(detail.timestamp).toLocaleString();
          html += `
            <tr>
              <td>${time}</td>
              <td>${detail.error_type}</td>
              <td title="${detail.message}">${detail.message.substring(0, 80)}...</td>
              <td title="${detail.page_url}">${detail.page_url.substring(0, 40)}...</td>
            </tr>
          `;
        });

        html += '</tbody></table>';
      }
    } catch (e) {
      // Ignore detail query errors
    }

    container.innerHTML = html;
  } catch (error) {
    container.innerHTML = `
      <div class="empty-state">
        <div class="empty-state-icon">❌</div>
        <p>查询失败: ${error.message}</p>
      </div>
    `;
  }
}

async function queryRenderer(startTime, endTime, pageUrl) {
  const container = document.getElementById('queryResult');
  container.innerHTML = '<div class="empty-state"><p>⏳ 正在查询...</p></div>';

  try {
    const result = await monitor.queryRendererSummary(startTime, endTime, pageUrl);
    const data = result.data;

    if (!data) {
      container.innerHTML = `
        <div class="empty-state">
          <div class="empty-state-icon">📭</div>
          <p>暂无渲染数据</p>
        </div>
      `;
      return;
    }

    const fpsStatus = data.avg_fps >= 50 ? 'good' : data.avg_fps >= 30 ? 'warning' : 'bad';

    container.innerHTML = `
      <div class="result-summary">
        <div class="result-item">
          <div class="label">平均 FPS</div>
          <div class="value metric-value ${fpsStatus}">${data.avg_fps}</div>
        </div>
        <div class="result-item">
          <div class="label">最低 FPS</div>
          <div class="value">${data.min_fps}</div>
        </div>
        <div class="result-item">
          <div class="label">最高 FPS</div>
          <div class="value">${data.max_fps}</div>
        </div>
        <div class="result-item">
          <div class="label">总卡顿次数</div>
          <div class="value">${data.total_jank}</div>
        </div>
        <div class="result-item">
          <div class="label">总长任务数</div>
          <div class="value">${data.total_long_tasks}</div>
        </div>
      </div>
    `;
  } catch (error) {
    container.innerHTML = `
      <div class="empty-state">
        <div class="empty-state-icon">❌</div>
        <p>查询失败: ${error.message}</p>
      </div>
    `;
  }
}

async function queryStats(startTime, endTime) {
  const container = document.getElementById('queryResult');
  container.innerHTML = '<div class="empty-state"><p>⏳ 正在查询...</p></div>';

  try {
    const result = await monitor.queryAppStats(startTime, endTime);
    const data = result.data;

    if (!data) {
      container.innerHTML = `
        <div class="empty-state">
          <div class="empty-state-icon">📭</div>
          <p>暂无统计数据</p>
        </div>
      `;
      return;
    }

    container.innerHTML = `
      <div class="result-summary">
        <div class="result-item">
          <div class="label">页面访问量 (PV)</div>
          <div class="value">${data.page_views}</div>
        </div>
        <div class="result-item">
          <div class="label">错误总数</div>
          <div class="value">${data.error_count}</div>
        </div>
      </div>
      <div style="margin-top:16px;padding:12px;background:#f5f7fa;border-radius:8px;font-size:13px;color:#666;">
        <strong>时间范围:</strong> ${new Date(data.time_range.start).toLocaleString()} ~ ${new Date(data.time_range.end).toLocaleString()}
      </div>
    `;
  } catch (error) {
    container.innerHTML = `
      <div class="empty-state">
        <div class="empty-state-icon">❌</div>
        <p>查询失败: ${error.message}</p>
      </div>
    `;
  }
}

function testJsError() {
  throw new Error('这是一个测试JS错误 - Test JS Error');
}

function testPromiseError() {
  Promise.reject(new Error('这是一个测试Promise拒绝 - Test Promise Rejection'));
}

function testResourceError() {
  const img = document.createElement('img');
  img.src = 'http://localhost:9999/nonexistent-image-' + Date.now() + '.png';
  img.onerror = () => {
    img.remove();
  };
  document.body.appendChild(img);
}

function testLongTask() {
  const start = Date.now();
  while (Date.now() - start < 200) {
    // 阻塞主线程
  }
  showNotification('⏱️ 已模拟200ms长任务');
}

function forceReportRenderer() {
  if (monitor && monitor.rendererMonitor) {
    monitor.reportRenderer();
    showNotification('📤 已上报渲染数据');
  }
}

window.addEventListener('load', () => {
  const now = new Date();
  const oneHourAgo = new Date(now.getTime() - 3600 * 1000);

  document.getElementById('startTime').value = formatDateTimeLocal(oneHourAgo);
  document.getElementById('endTime').value = formatDateTimeLocal(now);

  startMonitor();
});

function formatDateTimeLocal(date) {
  const pad = (n) => n.toString().padStart(2, '0');
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}T${pad(date.getHours())}:${pad(date.getMinutes())}`;
}

const style = document.createElement('style');
style.textContent = `
  @keyframes slideIn {
    from { transform: translateX(100%); opacity: 0; }
    to { transform: translateX(0); opacity: 1; }
  }
  @keyframes slideOut {
    from { transform: translateX(0); opacity: 1; }
    to { transform: translateX(100%); opacity: 0; }
  }
  .score-panel {
    display: flex;
    gap: 24px;
    margin-bottom: 24px;
    align-items: stretch;
  }
  .score-card {
    flex: 0 0 200px;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
    padding: 24px;
    border-radius: 12px;
    text-align: center;
    box-shadow: 0 4px 12px rgba(102, 126, 234, 0.3);
  }
  .score-label {
    font-size: 14px;
    opacity: 0.9;
    margin-bottom: 8px;
  }
  .score-value {
    font-size: 48px;
    font-weight: bold;
    line-height: 1;
    margin-bottom: 4px;
  }
  .score-grade {
    font-size: 20px;
    margin-bottom: 8px;
  }
  .score-trend {
    font-size: 14px;
    opacity: 0.9;
  }
  .grade-A { color: #4caf50 !important; }
  .grade-B { color: #8bc34a !important; }
  .grade-C { color: #ffeb3b !important; }
  .grade-D { color: #ff9800 !important; }
  .grade-F { color: #f44336 !important; }
  .score-details {
    flex: 1;
    background: white;
    border-radius: 12px;
    box-shadow: 0 2px 10px rgba(0,0,0,0.05);
    overflow: hidden;
  }
  .alert-panel {
    background: white;
    border-radius: 12px;
    padding: 24px;
    margin-bottom: 24px;
    box-shadow: 0 2px 10px rgba(0,0,0,0.05);
  }
  .alert-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 16px;
  }
  .alert-header h2 {
    font-size: 16px;
    margin: 0;
  }
  .alert-summary {
    font-size: 14px;
  }
  .alert-list {
    max-height: 200px;
    overflow-y: auto;
  }
  .alert-item {
    display: flex;
    align-items: flex-start;
    padding: 12px;
    margin-bottom: 8px;
    border-radius: 8px;
    background: #f5f7fa;
  }
  .alert-level-critical {
    background: #ffebee;
    border-left: 4px solid #f44336;
  }
  .alert-level-warning {
    background: #fff3e0;
    border-left: 4px solid #ff9800;
  }
  .alert-level-info {
    background: #e3f2fd;
    border-left: 4px solid #2196f3;
  }
  .alert-icon {
    font-size: 20px;
    margin-right: 12px;
  }
  .alert-content {
    flex: 1;
  }
  .alert-message {
    font-size: 14px;
    color: #333;
    margin-bottom: 4px;
  }
  .alert-meta {
    font-size: 12px;
    color: #999;
  }
  .export-panel {
    background: white;
    border-radius: 12px;
    padding: 24px;
    margin-bottom: 24px;
    box-shadow: 0 2px 10px rgba(0,0,0,0.05);
  }
  .export-panel h2 {
    font-size: 16px;
    margin-bottom: 16px;
  }
  .export-panel select {
    width: 100%;
    padding: 10px;
    border: 1px solid #ddd;
    border-radius: 6px;
    font-size: 14px;
  }
`;
document.head.appendChild(style);
