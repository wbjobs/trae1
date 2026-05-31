import { useState } from "react";
import { HardDrive, Upload, Download, FileJson, CheckCircle } from "lucide-react";
import { save, open } from "@tauri-apps/api/dialog";
import "./BackupRestore.css";

export default function BackupRestore({ connectedDevices, onBackup, onRestore }) {
  const [backupPath, setBackupPath] = useState("");
  const [restorePath, setRestorePath] = useState("");
  const [processing, setProcessing] = useState(false);

  const handleBackup = async () => {
    if (connectedDevices.length === 0) {
      alert("没有已连接的设备可以备份");
      return;
    }

    try {
      const filePath = await save({
        defaultPath: `scanner_backup_${Date.now()}.json`,
        filters: [
          {
            name: "JSON",
            extensions: ["json"],
          },
        ],
      });

      if (filePath) {
        setProcessing(true);
        setBackupPath(filePath);
        await onBackup(filePath);
      }
    } catch (e) {
      console.error("备份失败:", e);
    } finally {
      setProcessing(false);
    }
  };

  const handleRestore = async () => {
    try {
      const filePath = await open({
        filters: [
          {
            name: "JSON",
            extensions: ["json"],
          },
        ],
        multiple: false,
      });

      if (filePath) {
        setProcessing(true);
        setRestorePath(filePath);
        await onRestore(filePath);
      }
    } catch (e) {
      console.error("恢复失败:", e);
    } finally {
      setProcessing(false);
    }
  };

  return (
    <div className="backup-panel">
      <div className="backup-header">
        <HardDrive size={24} />
        <div>
          <h2>配置备份与恢复</h2>
          <p>保存和恢复扫描器配置到本地JSON文件</p>
        </div>
      </div>

      <div className="backup-content">
        <div className="backup-section">
          <div className="section-card">
            <div className="section-icon backup-icon">
              <Download size={28} />
            </div>
            <h3>备份配置</h3>
            <p className="section-description">
              将当前所有已连接设备的配置保存到本地JSON文件
            </p>

            <div className="device-summary">
              <div className="summary-item">
                <span className="summary-label">可备份设备</span>
                <span className="summary-value">{connectedDevices.length} 个</span>
              </div>
            </div>

            {backupPath && (
              <div className="path-display">
                <FileJson size={16} />
                <span className="path-label">上次备份:</span>
                <span className="path-value">{backupPath}</span>
              </div>
            )}

            <button
              className="btn btn-primary action-btn"
              onClick={handleBackup}
              disabled={connectedDevices.length === 0 || processing}
            >
              <Download size={18} />
              {processing ? "备份中..." : "导出配置"}
            </button>
          </div>

          <div className="section-card">
            <div className="section-icon restore-icon">
              <Upload size={28} />
            </div>
            <h3>恢复配置</h3>
            <p className="section-description">
              从备份文件中恢复设备配置，将尝试自动连接设备
            </p>

            <div className="restore-warning">
              <CheckCircle size={16} />
              <span>确保备份文件中的设备已连接到计算机</span>
            </div>

            {restorePath && (
              <div className="path-display">
                <FileJson size={16} />
                <span className="path-label">上次恢复:</span>
                <span className="path-value">{restorePath}</span>
              </div>
            )}

            <button
              className="btn btn-secondary action-btn"
              onClick={handleRestore}
              disabled={processing}
            >
              <Upload size={18} />
              {processing ? "恢复中..." : "导入配置"}
            </button>
          </div>
        </div>

        {connectedDevices.length > 0 && (
          <div className="devices-preview">
            <h3>可备份的设备</h3>
            <div className="devices-list">
              {connectedDevices.map((device) => (
                <div key={device.id} className="device-preview-item">
                  <div className="device-preview-icon">
                    <CheckCircle size={18} />
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
              ))}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
