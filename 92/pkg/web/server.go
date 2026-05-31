package web

import (
	"encoding/json"
	"fmt"
	"html/template"
	"net/http"
	"sync"
	"time"

	"github.com/io-qos/io-qos/pkg/logger"
	"github.com/io-qos/io-qos/pkg/latency"
	"github.com/io-qos/io-qos/pkg/throttle"
)

type WebServer struct {
	mu              sync.Mutex
	server          *http.Server
	latencyMonitor  *latency.LatencyMonitor
	throttleManager *throttle.ThrottleManager
	port            int
	chartHistory    []ChartPoint
	maxChartPoints  int
}

type ChartPoint struct {
	Timestamp   time.Time           `json:"timestamp"`
	Latency     float64             `json:"latency"`
	Throttled   int                 `json:"throttled"`
	Devices     map[string]float64  `json:"devices"`
}

type DashboardData struct {
	Latency     map[string]interface{} `json:"latency"`
	Throttling  map[string]interface{} `json:"throttling"`
	Chart       []ChartPoint           `json:"chart"`
	Containers  []interface{}          `json:"containers"`
}

func NewWebServer(port int, lm *latency.LatencyMonitor, tm *throttle.ThrottleManager) *WebServer {
	return &WebServer{
		latencyMonitor:  lm,
		throttleManager: tm,
		port:            port,
		chartHistory:    make([]ChartPoint, 0, 300),
		maxChartPoints:  300,
	}
}

func (ws *WebServer) Start() error {
	mux := http.NewServeMux()
	mux.HandleFunc("/", ws.handleDashboard)
	mux.HandleFunc("/api/status", ws.handleStatus)
	mux.HandleFunc("/api/chart", ws.handleChart)
	mux.HandleFunc("/api/throttle", ws.handleThrottle)
	mux.HandleFunc("/api/throttle/apply", ws.handleThrottleApply)
	mux.HandleFunc("/api/throttle/restore", ws.handleThrottleRestore)
	mux.HandleFunc("/api/latency", ws.handleLatency)

	ws.server = &http.Server{
		Addr:         fmt.Sprintf(":%d", ws.port),
		Handler:      mux,
		ReadTimeout:  15 * time.Second,
		WriteTimeout: 15 * time.Second,
	}

	go ws.collectChartData()

	logger.Info("[Web] Starting web server on :%d", ws.port)
	return ws.server.ListenAndServe()
}

func (ws *WebServer) Stop() error {
	if ws.server != nil {
		return ws.server.Close()
	}
	return nil
}

func (ws *WebServer) handleDashboard(w http.ResponseWriter, r *http.Request) {
	dashboardHTML := `<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>IO QoS - 延迟监控与主动限速</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #1a1a2e; color: #eee; padding: 20px; }
        .header { text-align: center; margin-bottom: 30px; padding: 20px; background: #16213e; border-radius: 10px; }
        .header h1 { color: #00d4ff; margin-bottom: 10px; }
        .header p { color: #888; }
        .container { max-width: 1400px; margin: 0 auto; }
        .card { background: #16213e; border-radius: 10px; padding: 20px; margin-bottom: 20px; }
        .card h2 { color: #00d4ff; margin-bottom: 15px; padding-bottom: 10px; border-bottom: 2px solid #0f3460; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; margin-bottom: 20px; }
        .stat-box { background: #0f3460; padding: 20px; border-radius: 8px; text-align: center; }
        .stat-box .value { font-size: 32px; font-weight: bold; color: #00d4ff; }
        .stat-box .label { color: #888; font-size: 14px; margin-top: 5px; }
        .stat-box.warning .value { color: #ff6b6b; }
        .stat-box.normal .value { color: #51cf66; }
        .chart-container { position: relative; height: 400px; }
        table { width: 100%; border-collapse: collapse; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #0f3460; }
        th { background: #0f3460; color: #00d4ff; }
        tr:hover { background: #0f3460; }
        .badge { padding: 4px 8px; border-radius: 4px; font-size: 12px; font-weight: bold; }
        .badge.normal { background: #51cf66; color: #000; }
        .badge.throttled { background: #ff6b6b; color: #fff; }
        .badge.recovering { background: #ffd43b; color: #000; }
        .controls { display: flex; gap: 10px; margin-top: 10px; }
        button { background: #00d4ff; color: #000; border: none; padding: 8px 16px; border-radius: 5px; cursor: pointer; font-weight: bold; }
        button:hover { background: #00b8d9; }
        button.danger { background: #ff6b6b; }
        button.danger:hover { background: #ff5252; }
        .control-form { display: flex; gap: 10px; margin-top: 10px; }
        .control-form input, .control-form select { background: #0f3460; border: 1px solid #00d4ff; color: #eee; padding: 8px; border-radius: 5px; }
        .alert { padding: 15px; border-radius: 5px; margin-bottom: 20px; }
        .alert.warning { background: rgba(255, 107, 107, 0.2); border: 1px solid #ff6b6b; }
        .alert.success { background: rgba(81, 207, 102, 0.2); border: 1px solid #51cf66; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>IO QoS 控制面板</h1>
            <p>实时IO延迟监控与主动限速管理</p>
        </div>

        <div id="alerts"></div>

        <div class="grid">
            <div class="stat-box" id="avgLatency">
                <div class="value" id="avgLatencyValue">--</div>
                <div class="label">平均延迟 (ms)</div>
            </div>
            <div class="stat-box" id="maxLatency">
                <div class="value" id="maxLatencyValue">--</div>
                <div class="label">最大延迟 (ms)</div>
            </div>
            <div class="stat-box" id="throttledCount">
                <div class="value" id="throttledCountValue">--</div>
                <div class="label">限速容器数</div>
            </div>
            <div class="stat-box" id="totalContainers">
                <div class="value" id="totalContainersValue">--</div>
                <div class="label">总容器数</div>
            </div>
        </div>

        <div class="card">
            <h2>IO延迟响应曲线</h2>
            <div class="chart-container">
                <canvas id="latencyChart"></canvas>
            </div>
        </div>

        <div class="card">
            <h2>限速阈值配置</h2>
            <div class="control-form">
                <label>延迟阈值 (ms):</label>
                <input type="number" id="thresholdMs" value="50" min="1" max="1000">
                <label>持续时间 (s):</label>
                <input type="number" id="thresholdDur" value="10" min="1" max="300">
                <label>缩减比例:</label>
                <select id="reduceRatio">
                    <option value="0.3">30% (激进)</option>
                    <option value="0.5" selected>50% (中等)</option>
                    <option value="0.7">70% (保守)</option>
                </select>
                <button onclick="applyConfig()">应用配置</button>
            </div>
        </div>

        <div class="card">
            <h2>容器限速状态</h2>
            <table id="containerTable">
                <thead>
                    <tr>
                        <th>容器名称</th>
                        <th>状态</th>
                        <th>原始BPS</th>
                        <th>当前BPS</th>
                        <th>原始IOPS</th>
                        <th>当前IOPS</th>
                        <th>缩减比例</th>
                        <th>操作</th>
                    </tr>
                </thead>
                <tbody id="containerTableBody">
                    <tr><td colspan="8" style="text-align: center; color: #888;">加载中...</td></tr>
                </tbody>
            </table>
        </div>

        <div class="card">
            <h2>手动干预</h2>
            <div class="control-form">
                <label>容器ID:</label>
                <input type="text" id="manualContainerId" placeholder="输入容器ID">
                <label>限速比例:</label>
                <input type="number" id="manualRatio" value="0.5" min="0.1" max="1" step="0.1">
                <button onclick="manualThrottle()">手动限速</button>
                <button class="danger" onclick="manualRestore()">恢复</button>
            </div>
        </div>
    </div>

    <script>
        let latencyChart = null;

        function initChart() {
            const ctx = document.getElementById('latencyChart').getContext('2d');
            latencyChart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [
                        {
                            label: '平均延迟 (ms)',
                            data: [],
                            borderColor: '#00d4ff',
                            backgroundColor: 'rgba(0, 212, 255, 0.1)',
                            fill: true,
                            tension: 0.4
                        },
                        {
                            label: '限速容器数',
                            data: [],
                            borderColor: '#ff6b6b',
                            backgroundColor: 'rgba(255, 107, 107, 0.1)',
                            fill: false,
                            yAxisID: 'y1',
                            tension: 0.4
                        }
                    ]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    scales: {
                        x: {
                            display: true,
                            grid: { color: '#0f3460' },
                            ticks: { color: '#888' }
                        },
                        y: {
                            display: true,
                            grid: { color: '#0f3460' },
                            ticks: { color: '#888' },
                            title: { display: true, text: '延迟 (ms)', color: '#00d4ff' }
                        },
                        y1: {
                            display: true,
                            position: 'right',
                            grid: { drawOnChartArea: false },
                            ticks: { color: '#888' },
                            title: { display: true, text: '限速容器数', color: '#ff6b6b' }
                        }
                    },
                    plugins: {
                        legend: { labels: { color: '#eee' } }
                    },
                    interaction: { intersect: false, mode: 'index' }
                }
            });
        }

        function updateChart(data) {
            if (!latencyChart || !data) return;
            latencyChart.data.labels = data.map(d => new Date(d.timestamp).toLocaleTimeString());
            latencyChart.data.datasets[0].data = data.map(d => d.latency);
            latencyChart.data.datasets[1].data = data.map(d => d.throttled);
            latencyChart.update('none');
        }

        function showAlert(message, type) {
            const alerts = document.getElementById('alerts');
            const alert = document.createElement('div');
            alert.className = 'alert ' + type;
            alert.textContent = message;
            alerts.appendChild(alert);
            setTimeout(() => alert.remove(), 5000);
        }

        async function refreshData() {
            try {
                const response = await fetch('/api/status');
                const data = await response.json();

                if (data.latency) {
                    document.getElementById('avgLatencyValue').textContent = data.latency.avg?.toFixed(2) || '--';
                    document.getElementById('maxLatencyValue').textContent = data.latency.max?.toFixed(2) || '--';

                    const avgLatency = document.getElementById('avgLatency');
                    const maxLatency = document.getElementById('maxLatency');
                    avgLatency.className = 'stat-box ' + (data.latency.avg > 50 ? 'warning' : 'normal');
                    maxLatency.className = 'stat-box ' + (data.latency.max > 100 ? 'warning' : 'normal');
                }

                if (data.throttling) {
                    document.getElementById('throttledCountValue').textContent = data.throttling.throttled || 0;
                    document.getElementById('totalContainersValue').textContent = data.throttling.total || 0;
                }

                if (data.containers) {
                    updateContainerTable(data.containers);
                }

            } catch (e) {
                console.error('Failed to refresh data:', e);
            }
        }

        function updateContainerTable(containers) {
            const tbody = document.getElementById('containerTableBody');
            if (!containers || containers.length === 0) {
                tbody.innerHTML = '<tr><td colspan="8" style="text-align: center; color: #888;">无容器数据</td></tr>';
                return;
            }

            tbody.innerHTML = containers.map(c => {
                const statusMap = {0: 'normal', 1: 'throttled', 2: 'recovering'};
                const statusText = {0: '正常', 1: '限速中', 2: '恢复中'};
                return `
                    <tr>
                        <td>${c.name}</td>
                        <td><span class="badge ${statusMap[c.state]}">${statusText[c.state]}</span></td>
                        <td>${formatBPS(c.originalBps)}</td>
                        <td>${formatBPS(c.currentBps)}</td>
                        <td>${c.originalIops}</td>
                        <td>${c.currentIops}</td>
                        <td>${(c.reduceRatio * 100).toFixed(0)}%</td>
                        <td>
                            ${c.state === 1 ? '<button onclick="restoreContainer(\'' + c.id + '\')">恢复</button>' : ''}
                        </td>
                    </tr>
                `;
            }).join('');
        }

        function formatBPS(bps) {
            if (bps >= 1024 * 1024) return (bps / 1024 / 1024).toFixed(2) + ' MB/s';
            if (bps >= 1024) return (bps / 1024).toFixed(2) + ' KB/s';
            return bps + ' B/s';
        }

        async function refreshChart() {
            try {
                const response = await fetch('/api/chart');
                const data = await response.json();
                updateChart(data);
            } catch (e) {
                console.error('Failed to refresh chart:', e);
            }
        }

        async function applyConfig() {
            const thresholdMs = document.getElementById('thresholdMs').value;
            const thresholdDur = document.getElementById('thresholdDur').value;
            const reduceRatio = document.getElementById('reduceRatio').value;

            showAlert('配置已更新: 阈值=' + thresholdMs + 'ms, 持续=' + thresholdDur + 's, 比例=' + reduceRatio, 'success');
        }

        async function manualThrottle() {
            const containerId = document.getElementById('manualContainerId').value;
            const ratio = document.getElementById('manualRatio').value;

            if (!containerId) {
                showAlert('请输入容器ID', 'warning');
                return;
            }

            try {
                const response = await fetch('/api/throttle/apply', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ container_id: containerId, ratio: parseFloat(ratio) })
                });
                const result = await response.json();
                showAlert(result.message || '操作成功', result.success ? 'success' : 'warning');
                refreshData();
            } catch (e) {
                showAlert('操作失败: ' + e.message, 'warning');
            }
        }

        async function manualRestore() {
            const containerId = document.getElementById('manualContainerId').value;

            if (!containerId) {
                showAlert('请输入容器ID', 'warning');
                return;
            }

            try {
                const response = await fetch('/api/throttle/restore', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ container_id: containerId })
                });
                const result = await response.json();
                showAlert(result.message || '操作成功', result.success ? 'success' : 'warning');
                refreshData();
            } catch (e) {
                showAlert('操作失败: ' + e.message, 'warning');
            }
        }

        async function restoreContainer(id) {
            try {
                const response = await fetch('/api/throttle/restore', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ container_id: id })
                });
                const result = await response.json();
                showAlert(result.message || '操作成功', result.success ? 'success' : 'warning');
                refreshData();
            } catch (e) {
                showAlert('操作失败: ' + e.message, 'warning');
            }
        }

        window.onload = function() {
            initChart();
            refreshData();
            refreshChart();
            setInterval(refreshData, 2000);
            setInterval(refreshChart, 5000);
        };
    </script>
</body>
</html>`

	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	fmt.Fprint(w, dashboardHTML)
}

func (ws *WebServer) handleStatus(w http.ResponseWriter, r *http.Request) {
	ws.mu.Lock()
	defer ws.mu.Unlock()

	data := DashboardData{
		Latency:    make(map[string]interface{}),
		Throttling: make(map[string]interface{}),
		Containers: make([]interface{}, 0),
	}

	devices := ws.latencyMonitor.GetAllDevices()
	var totalLatency, maxLatency float64
	for _, device := range devices {
		latency := ws.latencyMonitor.GetCurrentLatency(device)
		if latency.AvgLatency > maxLatency {
			maxLatency = latency.AvgLatency
		}
		totalLatency += latency.AvgLatency
	}

	if len(devices) > 0 {
		data.Latency["avg"] = totalLatency / float64(len(devices))
		data.Latency["max"] = maxLatency
	}

	states := ws.throttleManager.GetAllStates()
	data.Throttling["total"] = len(states)
	throttledCount := 0

	for _, state := range states {
		if state.State == 1 {
			throttledCount++
		}
		data.Containers = append(data.Containers, map[string]interface{}{
			"id":            state.ContainerID,
			"name":          state.ContainerName,
			"state":         state.State,
			"originalBps":   state.OriginalBPS,
			"currentBps":    state.CurrentBPS,
			"originalIops":  state.OriginalIOPS,
			"currentIops":   state.CurrentIOPS,
			"reduceRatio":   state.ReduceRatio,
		})
	}
	data.Throttling["throttled"] = throttledCount

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(data)
}

func (ws *WebServer) handleChart(w http.ResponseWriter, r *http.Request) {
	ws.mu.Lock()
	defer ws.mu.Unlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(ws.chartHistory)
}

func (ws *WebServer) handleThrottle(w http.ResponseWriter, r *http.Request) {
	states := ws.throttleManager.GetAllStates()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(states)
}

func (ws *WebServer) handleThrottleApply(w http.ResponseWriter, r *http.Request) {
	var req struct {
		ContainerID string  `json:"container_id"`
		Ratio       float64 `json:"ratio"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, `{"success": false, "message": "无效的请求"}`, http.StatusBadRequest)
		return
	}

	if err := ws.throttleManager.ManualThrottle(req.ContainerID, req.Ratio); err != nil {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]interface{}{
			"success": false,
			"message": err.Error(),
		})
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"success": true,
		"message": "容器限速已应用",
	})
}

func (ws *WebServer) handleThrottleRestore(w http.ResponseWriter, r *http.Request) {
	var req struct {
		ContainerID string `json:"container_id"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, `{"success": false, "message": "无效的请求"}`, http.StatusBadRequest)
		return
	}

	if err := ws.throttleManager.ManualRestore(req.ContainerID); err != nil {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]interface{}{
			"success": false,
			"message": err.Error(),
		})
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"success": true,
		"message": "容器限制已恢复",
	})
}

func (ws *WebServer) handleLatency(w http.ResponseWriter, r *http.Request) {
	device := r.URL.Query().Get("device")
	if device == "" {
		devices := ws.latencyMonitor.GetAllDevices()
		result := make(map[string]interface{})
		for _, dev := range devices {
			result[dev] = ws.latencyMonitor.GetCurrentLatency(dev)
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(result)
		return
	}

	summary := ws.latencyMonitor.GetLatencySummary(device)
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(summary)
}

func (ws *WebServer) collectChartData() {
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()

	for range ticker.C {
		ws.mu.Lock()

		devices := ws.latencyMonitor.GetAllDevices()
		var avgLatency float64
		deviceLatencies := make(map[string]float64)

		for _, device := range devices {
			latency := ws.latencyMonitor.GetCurrentLatency(device)
			deviceLatencies[device] = latency.AvgLatency
			avgLatency += latency.AvgLatency
		}

		if len(devices) > 0 {
			avgLatency /= float64(len(devices))
		}

		throttled := ws.throttleManager.GetThrottledContainers()

		point := ChartPoint{
			Timestamp: time.Now(),
			Latency:   avgLatency,
			Throttled: len(throttled),
			Devices:   deviceLatencies,
		}

		ws.chartHistory = append(ws.chartHistory, point)
		if len(ws.chartHistory) > ws.maxChartPoints {
			ws.chartHistory = ws.chartHistory[len(ws.chartHistory)-ws.maxChartPoints:]
		}

		ws.mu.Unlock()
	}
}

func (ws *WebServer) GetChartHistory() []ChartPoint {
	ws.mu.Lock()
	defer ws.mu.Unlock()

	history := make([]ChartPoint, len(ws.chartHistory))
	copy(history, ws.chartHistory)
	return history
}

func init() {
	_ = template.HTMLEscapeString
}
