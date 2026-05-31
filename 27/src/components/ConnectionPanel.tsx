import { useState, useEffect } from "react";
import {
  startSignalingServer,
  getServerInfo,
  getTransferProgress,
  getTransferStats,
  configureBackpressure,
  pauseTransfer,
  resumeTransfer,
} from "../tauriCommands";
import type {
  ServerInfo,
  TransferProgress,
  TransferStats,
  ConnectionInfo,
  BackpressureConfig,
} from "../types";

interface ConnectionPanelProps {
  role: "sender" | "receiver";
  onRoleChange: (role: "sender" | "receiver") => void;
}

export function ConnectionPanel({ role, onRoleChange }: ConnectionPanelProps) {
  const [port, setPort] = useState<number>(8080);
  const [serverInfo, setServerInfo] = useState<ServerInfo | null>(null);
  const [transferProgress, setTransferProgress] = useState<TransferProgress[]>([]);
  const [stats, setStats] = useState<TransferStats | null>(null);
  const [connecting, setConnecting] = useState<boolean>(false);
  const [connected, setConnected] = useState<boolean>(false);
  const [showBackpressure, setShowBackpressure] = useState<boolean>(false);
  const [lowWatermark, setLowWatermark] = useState<number>(16);
  const [highWatermark, setHighWatermark] = useState<number>(64);
  const [chunkSize, setChunkSize] = useState<number>(64);

  useEffect(() => {
    const interval = setInterval(async () => {
      try {
        const progress = await getTransferProgress();
        setTransferProgress(progress);

        const s = await getTransferStats();
        setStats(s);
      } catch (e) {
        // Silently ignore
      }
    }, 500);

    return () => clearInterval(interval);
  }, []);

  const handleStartServer = async () => {
    setConnecting(true);
    try {
      const info = await startSignalingServer({ port });
      setServerInfo(info);
      setConnected(true);
    } catch (e) {
      console.error("Failed to start server:", e);
    } finally {
      setConnecting(false);
    }
  };

  const handleRefreshServerInfo = async () => {
    try {
      const info = await getServerInfo();
      setServerInfo(info);
    } catch (e) {
      console.error("Failed to get server info:", e);
    }
  };

  const handleApplyBackpressure = async () => {
    try {
      const config: BackpressureConfig = {
        channel_id: "default",
        low_watermark_mb: lowWatermark,
        high_watermark_mb: highWatermark,
        chunk_size_kb: chunkSize,
      };
      await configureBackpressure(config);
      console.log("Backpressure settings applied:", config);
    } catch (e) {
      console.error("Failed to configure backpressure:", e);
    }
  };

  const handlePauseTransfer = async () => {
    try {
      await pauseTransfer("default");
    } catch (e) {
      console.error("Failed to pause transfer:", e);
    }
  };

  const handleResumeTransfer = async () => {
    try {
      await resumeTransfer("default");
    } catch (e) {
      console.error("Failed to resume transfer:", e);
    }
  };

  const formatBytes = (bytes: number): string => {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
    return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
  };

  const formatSpeed = (bps: number): string => {
    return `${formatBytes(bps)}/s`;
  };

  return (
    <div className="section">
      <div className="section-title">Connection</div>

      <div className="tabs">
        <div
          className={`tab ${role === "sender" ? "active" : ""}`}
          onClick={() => onRoleChange("sender")}
        >
          Sender
        </div>
        <div
          className={`tab ${role === "receiver" ? "active" : ""}`}
          onClick={() => onRoleChange("receiver")}
        >
          Receiver
        </div>
      </div>

      <div className="input-group">
        <label>Server Port</label>
        <input
          type="number"
          min="1024"
          max="65535"
          value={port}
          onChange={(e) => setPort(parseInt(e.target.value) || 8080)}
          disabled={connected}
        />
      </div>

      {!connected ? (
        <button
          className="btn btn-primary"
          onClick={handleStartServer}
          disabled={connecting}
        >
          {connecting ? "Starting..." : "Start Server"}
        </button>
      ) : (
        <button
          className="btn btn-secondary"
          onClick={handleRefreshServerInfo}
        >
          Refresh Status
        </button>
      )}

      <div style={{ marginTop: 12 }}>
        <div className="status-indicator status-connected">
          <div className="status-dot"></div>
          {connected ? "Server Running" : "Disconnected"}
        </div>
      </div>

      {serverInfo && (
        <div className="file-info" style={{ marginTop: 12 }}>
          <div className="file-info-row">
            <span className="file-info-label">Port:</span>
            <span className="file-info-value">{serverInfo.port}</span>
          </div>
          <div className="file-info-row">
            <span className="file-info-label">Connections:</span>
            <span className="file-info-value">{serverInfo.connections.length}</span>
          </div>
        </div>
      )}

      <div
        className="section-title"
        style={{
          marginTop: 16,
          cursor: "pointer",
          userSelect: "none",
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
        }}
        onClick={() => setShowBackpressure(!showBackpressure)}
      >
        <span>Backpressure Control</span>
        <span style={{ fontSize: 10 }}>{showBackpressure ? "▲" : "▼"}</span>
      </div>

      {showBackpressure && (
        <>
          <div className="input-group">
            <label>Low Watermark (MB)</label>
            <input
              type="number"
              min="1"
              max="1024"
              step="1"
              value={lowWatermark}
              onChange={(e) => setLowWatermark(parseFloat(e.target.value) || 16)}
            />
          </div>
          <div className="input-group">
            <label>High Watermark (MB)</label>
            <input
              type="number"
              min="1"
              max="2048"
              step="1"
              value={highWatermark}
              onChange={(e) => setHighWatermark(parseFloat(e.target.value) || 64)}
            />
          </div>
          <div className="input-group">
            <label>Chunk Size (KB)</label>
            <input
              type="number"
              min="4"
              max="1024"
              step="4"
              value={chunkSize}
              onChange={(e) => setChunkSize(parseInt(e.target.value) || 64)}
            />
          </div>
          <button
            className="btn btn-secondary"
            onClick={handleApplyBackpressure}
            style={{ fontSize: 12, padding: "6px 12px" }}
          >
            Apply Settings
          </button>

          <div className="controls-grid" style={{ marginTop: 12 }}>
            <button
              className="btn btn-secondary"
              onClick={handlePauseTransfer}
              style={{ fontSize: 11, padding: "4px 8px" }}
            >
              Pause
            </button>
            <button
              className="btn btn-secondary"
              onClick={handleResumeTransfer}
              style={{ fontSize: 11, padding: "4px 8px" }}
            >
              Resume
            </button>
          </div>
        </>
      )}

      {stats && (
        <div className="section" style={{ marginTop: 16 }}>
          <div className="section-title">Transfer Statistics</div>
          <div className="stats-grid">
            <div className="stat-item">
              <div className="stat-label">Sent</div>
              <div className="stat-value">{formatBytes(stats.total_sent)}</div>
            </div>
            <div className="stat-item">
              <div className="stat-label">Received</div>
              <div className="stat-value">{formatBytes(stats.total_received)}</div>
            </div>
            <div className="stat-item">
              <div className="stat-label">Speed</div>
              <div className="stat-value">{formatSpeed(stats.current_speed)}</div>
            </div>
            <div className="stat-item">
              <div className="stat-label">Peak</div>
              <div className="stat-value">{formatSpeed(stats.peak_speed)}</div>
            </div>
          </div>
          {stats.backpressure_events > 0 && (
            <div
              style={{
                marginTop: 8,
                padding: 6,
                background: "rgba(255, 165, 2, 0.1)",
                borderRadius: 4,
                fontSize: 10,
                color: "#ffa502",
                textAlign: "center",
              }}
            >
              Backpressure events: {stats.backpressure_events}
            </div>
          )}
          {stats.chunks_dropped > 0 && (
            <div
              style={{
                marginTop: 4,
                padding: 6,
                background: "rgba(255, 71, 87, 0.1)",
                borderRadius: 4,
                fontSize: 10,
                color: "#ff4757",
                textAlign: "center",
              }}
            >
              Dropped chunks: {stats.chunks_dropped}
            </div>
          )}
        </div>
      )}

      {transferProgress.length > 0 && (
        <div className="section" style={{ marginTop: 16 }}>
          <div className="section-title">Active Transfers</div>
          {transferProgress.map((progress) => (
            <div key={progress.transfer_id} className="file-info">
              <div className="file-info-row">
                <span className="file-info-label">Progress:</span>
                <span className="file-info-value">
                  {progress.progress_percent.toFixed(1)}%
                </span>
              </div>
              <div className="progress-bar">
                <div
                  className="progress-bar-fill"
                  style={{ width: `${progress.progress_percent}%` }}
                />
              </div>
              <div className="file-info-row">
                <span className="file-info-label">Speed:</span>
                <span className="file-info-value">
                  {formatSpeed(progress.bytes_per_second)}
                </span>
              </div>
              <div className="file-info-row">
                <span className="file-info-label">Transferred:</span>
                <span className="file-info-value">
                  {formatBytes(progress.transferred)} / {formatBytes(progress.total_size)}
                </span>
              </div>
              {progress.chunk_delay_ms > 0 && (
                <div
                  style={{
                    marginTop: 4,
                    padding: 4,
                    background: "rgba(255, 165, 2, 0.1)",
                    borderRadius: 4,
                    fontSize: 10,
                    color: "#ffa502",
                  }}
                >
                  Throttled: {progress.chunk_delay_ms}ms delay
                </div>
              )}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
