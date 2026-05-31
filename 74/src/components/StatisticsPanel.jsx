import { useState, useEffect, useCallback } from "react";
import { invoke } from "@tauri-apps/api";
import {
  BarChart3,
  TrendingUp,
  PieChart,
  Download,
  Calendar,
  RefreshCw,
} from "lucide-react";
import { save } from "@tauri-apps/api/dialog";
import "./StatisticsPanel.css";

export default function StatisticsPanel() {
  const [statistics, setStatistics] = useState(null);
  const [loading, setLoading] = useState(true);
  const [historyData, setHistoryData] = useState([]);
  const [dateRange, setDateRange] = useState("7");

  const loadStatistics = useCallback(async () => {
    setLoading(true);
    try {
      const stats = await invoke("get_scan_statistics");
      setStatistics(stats);
    } catch (e) {
      console.error("加载统计数据失败:", e);
    } finally {
      setLoading(false);
    }
  }, []);

  const loadHistory = useCallback(async () => {
    try {
      const days = parseInt(dateRange, 10);
      const data = await invoke("get_history_days", { days });
      setHistoryData(data);
    } catch (e) {
      console.error("加载历史数据失败:", e);
    }
  }, [dateRange]);

  const handleExport = async () => {
    try {
      const filePath = await save({
        defaultPath: `scan_report_${new Date().toISOString().slice(0, 10)}.csv`,
        filters: [{ name: "CSV", extensions: ["csv"] }],
      });

      if (filePath) {
        const count = await invoke("export_records_csv", {
          request: { file_path: filePath },
        });
        alert(`成功导出 ${count} 条记录到:\n${filePath}`);
      }
    } catch (e) {
      alert(`导出失败: ${e}`);
    }
  };

  useEffect(() => {
    loadStatistics();
    loadHistory();
  }, [loadStatistics, loadHistory]);

  const formatInterval = (ms) => {
    if (ms < 1000) return `${ms.toFixed(0)}ms`;
    return `${(ms / 1000).toFixed(1)}s`;
  };

  const maxHourlyCount = Math.max(...historyData.map((d) => d.count), 1);

  if (loading) {
    return (
      <div className="statistics-panel">
        <div className="loading-state">
          <RefreshCw className="spinning" size={32} />
          <p>正在加载统计数据...</p>
        </div>
      </div>
    );
  }

  return (
    <div className="statistics-panel">
      <div className="statistics-header">
        <div className="statistics-title">
          <BarChart3 size={24} />
          <div>
            <h2>数据统计</h2>
            <p>扫描数据统计与趋势分析</p>
          </div>
        </div>
        <div className="header-actions">
          <div className="date-range-selector">
            <Calendar size={16} />
            <select
              value={dateRange}
              onChange={(e) => setDateRange(e.target.value)}
              className="range-select"
            >
              <option value="7">最近7天</option>
              <option value="14">最近14天</option>
              <option value="30">最近30天</option>
            </select>
          </div>
          <button className="btn btn-secondary btn-sm" onClick={loadStatistics}>
            <RefreshCw size={14} />
            刷新
          </button>
          <button className="btn btn-primary btn-sm" onClick={handleExport}>
            <Download size={14} />
            导出CSV
          </button>
        </div>
      </div>

      {statistics && (
        <>
          <div className="stats-overview">
            <div className="stat-card">
              <div className="stat-card-header">
                <TrendingUp size={20} />
                <span>总扫描次数</span>
              </div>
              <div className="stat-card-value">{statistics.total_count.toLocaleString()}</div>
              <div className="stat-card-footer">
                今日: <strong>{statistics.today_count.toLocaleString()}</strong>
              </div>
            </div>

            <div className="stat-card success">
              <div className="stat-card-header">
                <span>成功率</span>
              </div>
              <div className="stat-card-value">{statistics.success_rate.toFixed(1)}%</div>
              <div className="stat-card-footer">
                成功: {statistics.success_count.toLocaleString()}
              </div>
            </div>

            <div className="stat-card">
              <div className="stat-card-header">
                <span>平均扫描间隔</span>
              </div>
              <div className="stat-card-value">
                {formatInterval(statistics.avg_interval_ms)}
              </div>
              <div className="stat-card-footer">
                失败次数: {statistics.failed_count.toLocaleString()}
              </div>
            </div>

            <div className="stat-card target">
              <div className="stat-card-header">
                <span>今日进度</span>
              </div>
              <div className="stat-card-value">
                {statistics.today_progress.toFixed(1)}%
              </div>
              <div className="stat-card-footer">
                目标: {statistics.today_target.toLocaleString()}
              </div>
            </div>
          </div>

          <div className="stats-content">
            <div className="stats-section">
              <div className="section-header">
                <PieChart size={18} />
                <h3>按条码类型统计</h3>
              </div>

              {statistics.by_type.length === 0 ? (
                <div className="empty-section">
                  <p>暂无数据</p>
                </div>
              ) : (
                <div className="type-list">
                  {statistics.by_type.map((item) => (
                    <div key={item.type_name} className="type-item">
                      <div className="type-info">
                        <span className="type-name">{item.type_name}</span>
                        <span className="type-count">{item.count.toLocaleString()} 次</span>
                      </div>
                      <div className="type-bar">
                        <div
                          className="type-bar-fill"
                          style={{
                            width: `${item.percentage}%`,
                            backgroundColor: getTypeColor(item.type_name),
                          }}
                        />
                      </div>
                      <span className="type-percentage">
                        {item.percentage.toFixed(1)}%
                      </span>
                    </div>
                  ))}
                </div>
              )}
            </div>

            <div className="stats-section">
              <div className="section-header">
                <BarChart3 size={18} />
                <h3>扫描趋势（按小时）</h3>
              </div>

              {historyData.length === 0 ? (
                <div className="empty-section">
                  <p>暂无趋势数据</p>
                </div>
              ) : (
                <div className="trend-chart">
                  <div className="chart-bars">
                    {Array.from({ length: 24 }, (_, hour) => {
                      const hourData = historyData.find((d) => d.hour === hour);
                      const count = hourData?.count || 0;
                      const height = (count / maxHourlyCount) * 100;
                      return (
                        <div key={hour} className="chart-bar-wrapper">
                          <div
                            className="chart-bar"
                            style={{
                              height: `${Math.max(height, 2)}%`,
                              backgroundColor:
                                count > 0 ? "var(--accent)" : "var(--bg-tertiary)",
                            }}
                          >
                            {count > 0 && (
                              <span className="bar-value">{count}</span>
                            )}
                          </div>
                          <span className="bar-label">{hour}</span>
                        </div>
                      );
                    })}
                  </div>
                </div>
              )}
            </div>
          </div>
        </>
      )}
    </div>
  );
}

function getTypeColor(typeName) {
  const colors = {
    "EAN-13": "#4ecca3",
    "Code 128": "#54a0ff",
    "QR Code": "#ff9f43",
    "Code 39": "#a55eea",
    "EAN-8": "#26de81",
    "UPC-A": "#fd79a8",
    "UPC-E": "#fd9644",
    "Code 93": "#45aaf2",
    "ITF": "#fc5c65",
    "Codabar": "#2bcbba",
    "Data Matrix": "#eb3b5a",
    "PDF417": "#8854d0",
    "Unknown": "#95a5a6",
  };
  return colors[typeName] || "#6c757d";
}
