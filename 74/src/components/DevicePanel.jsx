import { Usb, Link, Unlink, RefreshCw, CheckCircle, XCircle } from "lucide-react";
import "./DevicePanel.css";

export default function DevicePanel({
  availableDevices,
  connectedDevices,
  selectedDeviceId,
  onConnect,
  onDisconnect,
  onSelectDevice,
  loading,
}) {
  return (
    <div className="device-panel">
      <div className="panel-section">
        <div className="section-header">
          <h2>
            <Usb size={20} />
            可用设备
          </h2>
          <span className="device-badge">{availableDevices.length} 个设备</span>
        </div>

        {loading ? (
          <div className="loading-state">
            <RefreshCw className="spinning" size={32} />
            <p>正在扫描USB设备...</p>
          </div>
        ) : availableDevices.length === 0 ? (
          <div className="empty-state">
            <Usb size={48} />
            <p>未检测到USB设备</p>
            <p className="hint">请确保设备已正确连接并点击"刷新设备"</p>
          </div>
        ) : (
          <div className="device-grid">
            {availableDevices.map((device) => (
              <div
                key={device.id}
                className={`device-card ${device.connected ? "connected" : ""}`}
              >
                <div className="device-header">
                  <div className="device-icon">
                    <Usb size={24} />
                  </div>
                  <div className="device-status">
                    {device.connected ? (
                      <span className="status-connected">
                        <CheckCircle size={14} />
                        已连接
                      </span>
                    ) : (
                      <span className="status-disconnected">
                        <XCircle size={14} />
                        未连接
                      </span>
                    )}
                  </div>
                </div>

                <div className="device-info">
                  <h3>{device.product || "未知设备"}</h3>
                  <p className="manufacturer">
                    {device.manufacturer || "未知制造商"}
                  </p>
                  <div className="device-details">
                    <span>VID: 0x{device.vendor_id.toString(16).padStart(4, "0").toUpperCase()}</span>
                    <span>PID: 0x{device.product_id.toString(16).padStart(4, "0").toUpperCase()}</span>
                  </div>
                  {device.serial_number && (
                    <p className="serial">SN: {device.serial_number}</p>
                  )}
                </div>

                <div className="device-actions">
                  {device.connected ? (
                    <button
                      className="btn btn-danger btn-sm"
                      onClick={() => onDisconnect(device.id)}
                    >
                      <Unlink size={14} />
                      断开
                    </button>
                  ) : (
                    <button
                      className="btn btn-primary btn-sm"
                      onClick={() => onConnect(device)}
                    >
                      <Link size={14} />
                      连接
                    </button>
                  )}
                </div>
              </div>
            ))}
          </div>
        )}
      </div>

      <div className="panel-section">
        <div className="section-header">
          <h2>
            <CheckCircle size={20} />
            已连接设备
          </h2>
          <span className="device-badge">{connectedDevices.length}/5</span>
        </div>

        {connectedDevices.length === 0 ? (
          <div className="empty-state">
            <CheckCircle size={48} />
            <p>暂无已连接设备</p>
            <p className="hint">从上方列表选择设备并连接</p>
          </div>
        ) : (
          <div className="connected-list">
            {connectedDevices.map((device) => (
              <div
                key={device.id}
                className={`connected-item ${
                  selectedDeviceId === device.id ? "selected" : ""
                }`}
                onClick={() => onSelectDevice(device.id)}
              >
                <div className="connected-info">
                  <div className="connected-icon">
                    <Usb size={20} />
                  </div>
                  <div>
                    <h4>{device.product || "未知设备"}</h4>
                    <p>
                      VID: 0x{device.vendor_id.toString(16).padStart(4, "0").toUpperCase()}
                      {" | "}
                      PID: 0x{device.product_id.toString(16).padStart(4, "0").toUpperCase()}
                    </p>
                  </div>
                </div>
                <button
                  className="btn btn-danger btn-sm"
                  onClick={(e) => {
                    e.stopPropagation();
                    onDisconnect(device.id);
                  }}
                >
                  <Unlink size={14} />
                </button>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
