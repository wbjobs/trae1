import { useState, useEffect } from "react";
import { invoke } from "@tauri-apps/api";
import { listen } from "@tauri-apps/api/event";
import Sidebar from "./components/Sidebar.jsx";
import MonitorPanel from "./components/MonitorPanel.jsx";
import StatisticsPanel from "./components/StatisticsPanel.jsx";
import DevicePanel from "./components/DevicePanel.jsx";
import ConfigPanel from "./components/ConfigPanel.jsx";
import PresetTemplates from "./components/PresetTemplates.jsx";
import BackupRestore from "./components/BackupRestore.jsx";
import Toast from "./components/Toast.jsx";
import "./App.css";

export default function App() {
  const [activeTab, setActiveTab] = useState("monitor");
  const [availableDevices, setAvailableDevices] = useState([]);
  const [connectedDevices, setConnectedDevices] = useState([]);
  const [selectedDeviceId, setSelectedDeviceId] = useState(null);
  const [currentConfig, setCurrentConfig] = useState(null);
  const [deviceCapabilities, setDeviceCapabilities] = useState(null);
  const [toast, setToast] = useState(null);
  const [loading, setLoading] = useState(false);
  const [writeError, setWriteError] = useState(null);

  const showToast = (message, type = "info") => {
    setToast({ message, type });
    setTimeout(() => setToast(null), 5000);
  };

  const refreshDevices = async () => {
    setLoading(true);
    try {
      const devices = await invoke("list_devices");
      setAvailableDevices(devices);
    } catch (e) {
      showToast(`扫描设备失败: ${e}`, "error");
    } finally {
      setLoading(false);
    }
  };

  const refreshConnectedDevices = async () => {
    try {
      const devices = await invoke("get_connected_devices");
      setConnectedDevices(devices);
    } catch (e) {
      console.error("获取已连接设备失败:", e);
    }
  };

  const loadCapabilities = async (deviceId) => {
    try {
      const caps = await invoke("get_device_capabilities", {
        request: { device_id: deviceId },
      });
      setDeviceCapabilities(caps);
    } catch (e) {
      console.warn("获取设备能力失败:", e);
      setDeviceCapabilities(null);
    }
  };

  const connectDevice = async (device) => {
    setLoading(true);
    setWriteError(null);
    try {
      const info = await invoke("connect_device", {
        request: {
          vendor_id: device.vendor_id,
          product_id: device.product_id,
          path: device.path,
        },
      });
      showToast(`设备已连接: ${info.product || "未知设备"}`, "success");
      await refreshDevices();
      await refreshConnectedDevices();
      setSelectedDeviceId(info.id);
      await loadConfig(info.id);
      await loadCapabilities(info.id);
    } catch (e) {
      showToast(`连接失败: ${e}`, "error");
    } finally {
      setLoading(false);
    }
  };

  const disconnectDevice = async (deviceId) => {
    setWriteError(null);
    try {
      await invoke("disconnect_device", {
        request: { device_id: deviceId },
      });
      showToast("设备已断开", "info");
      if (selectedDeviceId === deviceId) {
        setSelectedDeviceId(null);
        setCurrentConfig(null);
        setDeviceCapabilities(null);
      }
      await refreshDevices();
      await refreshConnectedDevices();
    } catch (e) {
      showToast(`断开失败: ${e}`, "error");
    }
  };

  const loadConfig = async (deviceId) => {
    setWriteError(null);
    try {
      const config = await invoke("get_device_config", {
        request: { device_id: deviceId },
      });
      setCurrentConfig(config);
      setSelectedDeviceId(deviceId);
    } catch (e) {
      showToast(`加载配置失败: ${e}`, "error");
    }
  };

  const saveConfig = async (deviceId, config) => {
    setWriteError(null);
    try {
      const result = await invoke("set_device_config", {
        request: { device_id: deviceId, config },
      });

      if (result.success) {
        const retryMsg =
          result.retries_used > 0
            ? `（重试${result.retries_used}次）`
            : "";
        showToast(`配置已保存${retryMsg}`, "success");
        if (result.config) {
          setCurrentConfig(result.config);
        }
      } else {
        let errorMsg = result.message;
        if (result.error && result.error.details) {
          const d = result.error.details;
          if (d.field_name && d.limit > 0) {
            errorMsg = `${result.message}（字段"${d.field_name}": ${d.actual_size}字节 / 限制${d.limit}字节）`;
          }
          if (d.retry_count > 0) {
            errorMsg += `（已重试${d.retry_count}次）`;
          }
        }
        if (result.rolled_back) {
          errorMsg += "；配置已回滚";
        }
        showToast(errorMsg, "error");
        setWriteError(result.error);
        if (result.config) {
          setCurrentConfig(result.config);
        }
      }
    } catch (e) {
      showToast(`保存配置失败: ${e}`, "error");
    }
  };

  const applyPreset = async (deviceId, presetName) => {
    setWriteError(null);
    try {
      const result = await invoke("apply_preset", {
        request: { device_id: deviceId, preset_name: presetName },
      });

      if (result.success) {
        showToast(`已应用预设: ${presetName}`, "success");
        if (result.config) {
          setCurrentConfig(result.config);
        }
      } else {
        showToast(`应用预设失败: ${result.message}`, "error");
        setWriteError(result.error);
        if (result.config && result.rolled_back) {
          setCurrentConfig(result.config);
        }
      }
    } catch (e) {
      showToast(`应用预设失败: ${e}`, "error");
    }
  };

  const handleBackup = async (filePath) => {
    try {
      await invoke("backup_config", {
        request: { file_path: filePath },
      });
      showToast(`配置已备份到: ${filePath}`, "success");
    } catch (e) {
      showToast(`备份失败: ${e}`, "error");
    }
  };

  const handleRestore = async (filePath) => {
    try {
      const restored = await invoke("restore_config", {
        request: { file_path: filePath },
      });
      showToast(`已恢复 ${restored.length} 个设备配置`, "success");
      await refreshDevices();
      await refreshConnectedDevices();
    } catch (e) {
      showToast(`恢复失败: ${e}`, "error");
    }
  };

  useEffect(() => {
    refreshDevices();
    refreshConnectedDevices();

    const unlistenPromise = listen("device_disconnected", (event) => {
      showToast("设备连接中断，正在尝试重连...", "warning");
      const deviceId = event.payload;
      setTimeout(async () => {
        try {
          await refreshConnectedDevices();
          showToast("设备已重连", "success");
        } catch {
          showToast("设备重连失败", "error");
        }
      }, 3000);
    });

    return () => {
      unlistenPromise.then((f) => f());
    };
  }, []);

  const selectedDevice = connectedDevices.find((d) => d.id === selectedDeviceId);

  return (
    <div className="app-container">
      <Sidebar activeTab={activeTab} onTabChange={setActiveTab} />
      <main className="main-content">
        <header className="app-header">
          <h1>
            <span className="header-icon">📷</span>
            条码扫描器管理器
          </h1>
          <div className="header-info">
            <span className="device-count">
              已连接: {connectedDevices.length}/5
            </span>
            <button
              className="btn btn-secondary"
              onClick={refreshDevices}
              disabled={loading}
            >
              {loading ? "扫描中..." : "刷新设备"}
            </button>
          </div>
        </header>

        {writeError && (
          <div className="error-banner">
            <div className="error-content">
              <strong>⚠️ 写入错误</strong>
              <p>{writeError.message}</p>
              {writeError.details && (
                <div className="error-details">
                  {writeError.details.field_name && (
                    <span>
                      字段: <code>{writeError.details.field_name}</code>
                    </span>
                  )}
                  {writeError.details.limit > 0 && (
                    <span>
                      限制: <code>{writeError.details.limit}字节</code>
                    </span>
                  )}
                  {writeError.details.actual_size > 0 && (
                    <span>
                      实际: <code>{writeError.details.actual_size}字节</code>
                    </span>
                  )}
                  {writeError.details.retry_count > 0 && (
                    <span>
                      重试次数: <code>{writeError.details.retry_count}</code>
                    </span>
                  )}
                </div>
              )}
            </div>
            <button
              className="error-close"
              onClick={() => setWriteError(null)}
            >
              ✕
            </button>
          </div>
        )}

        <div className="content-area">
          {activeTab === "monitor" && (
            <MonitorPanel
              connectedDevices={connectedDevices}
              selectedDeviceId={selectedDeviceId}
              onDeviceSelect={(id) => {
                setSelectedDeviceId(id);
                loadConfig(id);
                loadCapabilities(id);
              }}
            />
          )}

          {activeTab === "statistics" && (
            <StatisticsPanel />
          )}

          {activeTab === "devices" && (
            <DevicePanel
              availableDevices={availableDevices}
              connectedDevices={connectedDevices}
              selectedDeviceId={selectedDeviceId}
              onConnect={connectDevice}
              onDisconnect={disconnectDevice}
              onSelectDevice={(id) => {
                loadConfig(id);
                loadCapabilities(id);
              }}
              loading={loading}
            />
          )}

          {activeTab === "config" && (
            <ConfigPanel
              device={selectedDevice}
              config={currentConfig}
              capabilities={deviceCapabilities}
              onSave={saveConfig}
            />
          )}

          {activeTab === "presets" && (
            <PresetTemplates
              device={selectedDevice}
              onApply={applyPreset}
            />
          )}

          {activeTab === "backup" && (
            <BackupRestore
              connectedDevices={connectedDevices}
              onBackup={handleBackup}
              onRestore={handleRestore}
            />
          )}
        </div>
      </main>
      {toast && <Toast message={toast.message} type={toast.type} />}
    </div>
  );
}
