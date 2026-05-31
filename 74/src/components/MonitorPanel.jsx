import { useState, useEffect, useCallback } from "react";
import { invoke } from "@tauri-apps/api";
import { Activity, Barcode, Clock, CheckCircle, XCircle, Target, Zap } from "lucide-react";
import "./MonitorPanel.css";

export default function MonitorPanel({
  connectedDevices,
  selectedDeviceId,
  onDeviceSelect,
}) {
  const [recentRecords, setRecentRecords] = useState([]);
  const [dailyTarget, setDailyTarget] = useState(null);
  const [newTarget, setNewTarget] = useState("");
  const [editingTarget, setEditingTarget] = useState(false);
  const [simulatedBarcode, setSimulatedBarcode] = useState("");
  const [lastScanTime, setLastScanTime] = useState(null);

  const loadRecentRecords = useCallback(async () => {
    try {
      const records = await invoke("get_recent_records", { limit: 50 });
      setRecentRecords(records);
    } catch (e) {
      console.error("加载记录失败:", e);
    }
  }, []);

  const loadDailyTarget = useCallback(async () => {
    try {
      const target = await invoke("get_daily_target_info");
      setDailyTarget(target);
      setNewTarget(target.target.toString());
    } catch (e) {
      console.error("加载目标失败:", e);
    }
  }, []);

  const handleSimulateScan = async () => {
    if (!simulatedBarcode.trim() || !selectedDeviceId) return;

    try {
      const record = await invoke("add_scan_record", {
        request: {
          device_id: selectedDeviceId,
          barcode: simulatedBarcode.trim(),
          success: true,
          duration_ms: Math.floor(Math.random() * 50 + 10),
        },
      });

      setRecentRecords((prev) => [record, ...prev].slice(0, 50));
      setSimulatedBarcode("");
      setLastScanTime(new Date());
      loadDailyTarget();
    } catch (e) {
      console.error("添加记录失败:", e);
    }
  };

  const handleSetTarget = async () => {
    const target = parseInt(newTarget, 10);
    if (isNaN(target) || target < 0) return;

    try {
      await invoke("set_daily_target", {
        request: { target },
      });
      setEditingTarget(false);
      loadDailyTarget();
    } catch (e) {
      console.error("设置目标失败:", e);
    }
  };

  useEffect(() => {
    loadRecentRecords();
    loadDailyTarget();

    const interval = setInterval(() => {
      loadRecentRecords();
      loadDailyTarget();
    }, 5000);

    return () => clearInterval(interval);
  }, [loadRecentRecords, loadDailyTarget]);

  const formatTime = (timestamp) => {
    try {
      const date = new Date(timestamp);
      return date.toLocaleTimeString("zh-CN", {
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
      });
    } catch {
      return timestamp;
    }
  };

  const getBarcodeTypeColor = (type) => {
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
    return colors[type] || "#95a5a6";
  };

  return (
    <div className="monitor-panel">
      <div className="monitor-header">
        <div className="monitor-title">
          <Activity size={24} />
          <div>
            <h2>实时监控</h2>
            <p>扫描数据实时显示与统计</p>
          </div>
        </div>
        {lastScanTime && (
          <div className="last-scan-time">
            <Clock size={14} />
            <span>最后扫描: {lastScanTime.toLocaleTimeString("zh-CN")}</span>
          </div>
        )}
      </div>

      <div className="daily-target-section">
        <div className="target-card">
          <div className="target-header">
            <Target size={20} />
            <h3>今日目标</h3>
            {dailyTarget && (
              <button
                className="edit-target-btn"
                onClick={() => setEditingTarget(!editingTarget)}
              >
                {editingTarget ? "取消" : "修改"}
              </button>
            )}
          </div>

          {dailyTarget ? (
            <>
              {editingTarget ? (
                <div className="target-edit">
                  <input
                    type="number"
                    value={newTarget}
                    onChange={(e) => setNewTarget(e.target.value)}
                    min="0"
                    className="target-input"
                  />
                  <button className="btn btn-primary btn-sm" onClick={handleSetTarget}>
                    确认
                  </button>
                </div>
              ) : (
                <div className="target-display">
                  <div className="target-progress">
                    <div className="progress-bar">
                      <div
                        className="progress-fill"
                        style={{
                          width: `${Math.min(dailyTarget.progress, 100)}%`,
                          backgroundColor:
                            dailyTarget.progress >= 100
                              ? "var(--success)"
                              : "var(--accent)",
                        }}
                      />
                    </div>
                    <span className="progress-text">
                      {dailyTarget.current} / {dailyTarget.target}
                    </span>
                  </div>
                  <div className="target-percentage">
                    {dailyTarget.progress.toFixed(1)}%
                    {dailyTarget.progress >= 100 && (
                      <span className="target-complete"> 🎉 已完成</span>
                    )}
                  </div>
                </div>
              )}
            </>
          ) : (
            <div className="target-loading">加载中...</div>
          )}
        </div>

        <div className="quick-stats">
          <div className="stat-item">
            <div className="stat-icon" style={{ backgroundColor: "rgba(78, 204, 163, 0.2)" }}>
              <Zap size={20} style={{ color: "var(--success)" }} />
            </div>
            <div className="stat-info">
              <span className="stat-label">今日扫描</span>
              <span className="stat-value">{dailyTarget?.current || 0}</span>
            </div>
          </div>
          <div className="stat-item">
            <div className="stat-icon" style={{ backgroundColor: "rgba(84, 160, 255, 0.2)" }}>
              <CheckCircle size={20} style={{ color: "#54a0ff" }} />
            </div>
            <div className="stat-info">
              <span className="stat-label">累计扫描</span>
              <span className="stat-value">{dailyTarget?.total_count || 0}</span>
            </div>
          </div>
        </div>
      </div>

      {connectedDevices.length > 0 && (
        <div className="simulate-section">
          <div className="simulate-header">
            <Barcode size={18} />
            <span>模拟扫描（测试用）</span>
          </div>
          <div className="simulate-input">
            <select
              value={selectedDeviceId || ""}
              onChange={(e) => onDeviceSelect(e.target.value)}
              className="device-select"
            >
              <option value="">选择设备...</option>
              {connectedDevices.map((d) => (
                <option key={d.id} value={d.id}>
                  {d.product || "未知设备"}
                </option>
              ))}
            </select>
            <input
              type="text"
              value={simulatedBarcode}
              onChange={(e) => setSimulatedBarcode(e.target.value)}
              placeholder="输入条码内容..."
              className="simulate-barcode-input"
              onKeyDown={(e) => e.key === "Enter" && handleSimulateScan()}
            />
            <button
              className="btn btn-primary btn-sm"
              onClick={handleSimulateScan}
              disabled={!selectedDeviceId || !simulatedBarcode.trim()}
            >
              模拟扫描
            </button>
          </div>
        </div>
      )}

      <div className="records-section">
        <div className="section-title">
          <h3>最近扫描记录</h3>
          <span className="record-count">{recentRecords.length} 条</span>
        </div>

        {recentRecords.length === 0 ? (
          <div className="empty-records">
            <Barcode size={32} />
            <p>暂无扫描记录</p>
            <p className="hint">连接设备后开始扫描</p>
          </div>
        ) : (
          <div className="records-table">
            <div className="table-header">
              <span>时间</span>
              <span>设备</span>
              <span>条码内容</span>
              <span>类型</span>
              <span>状态</span>
              <span>耗时</span>
            </div>
            <div className="table-body">
              {recentRecords.map((record) => {
                const device = connectedDevices.find((d) => d.id === record.device_id);
                return (
                  <div key={record.id} className="table-row">
                    <span className="time-cell">{formatTime(record.timestamp)}</span>
                    <span className="device-cell">
                      {device?.product || record.device_id.slice(0, 8)}
                    </span>
                    <span className="barcode-cell">
                      <code>{record.barcode}</code>
                    </span>
                    <span
                      className="type-cell"
                      style={{ color: getBarcodeTypeColor(record.barcode_type) }}
                    >
                      {record.barcode_type}
                    </span>
                    <span className="status-cell">
                      {record.success ? (
                        <span className="status-success">
                          <CheckCircle size={14} /> 成功
                        </span>
                      ) : (
                        <span className="status-failed">
                          <XCircle size={14} /> 失败
                        </span>
                      )}
                    </span>
                    <span className="duration-cell">{record.duration_ms}ms</span>
                  </div>
                );
              })}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
