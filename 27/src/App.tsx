import { useState } from "react";
import { PointCloudViewer } from "./components/PointCloudViewer";
import { FileLoader } from "./components/FileLoader";
import { DownsamplingControls } from "./components/DownsamplingControls";
import { ConnectionPanel } from "./components/ConnectionPanel";
import { TransferControls } from "./components/TransferControls";
import { RegistrationPanel } from "./components/RegistrationPanel";
import { RegistrationToolbar } from "./components/RegistrationToolbar";
import type { PointCloud, RegistrationResult } from "./types";
import { applyHeightColoring } from "./tauriCommands";

type Role = "sender" | "receiver";
type ColorMode = "height" | "original";
type ViewMode = "source" | "target" | "merged";

export default function App() {
  const [pointCloud, setPointCloud] = useState<PointCloud | null>(null);
  const [targetPointCloud, setTargetPointCloud] = useState<PointCloud | null>(null);
  const [mergedPointCloud, setMergedPointCloud] = useState<PointCloud | null>(null);
  const [registrationResult, setRegistrationResult] = useState<RegistrationResult | null>(null);
  const [role, setRole] = useState<Role>("sender");
  const [colorMode, setColorMode] = useState<ColorMode>("height");
  const [viewMode, setViewMode] = useState<ViewMode>("source");
  const [originalCount, setOriginalCount] = useState<number>(0);
  const [targetPointCount, setTargetPointCount] = useState<number>(0);
  const [loading, setLoading] = useState<boolean>(false);

  const handlePointCloudLoaded = (cloud: PointCloud, origCount: number) => {
    setPointCloud(cloud);
    setOriginalCount(origCount);
    setViewMode("source");
  };

  const handleTargetPointCloudLoaded = (cloud: PointCloud, pointCount: number) => {
    setTargetPointCloud(cloud);
    setTargetPointCount(pointCount);
  };

  const handleDownsample = (cloud: PointCloud, origCount: number, _downsampledCount: number) => {
    setPointCloud(cloud);
    setOriginalCount(origCount);
  };

  const handlePointCloudReceived = (cloud: PointCloud) => {
    setPointCloud(cloud);
  };

  const handleRegistrationComplete = (result: RegistrationResult, merged: PointCloud | null) => {
    setRegistrationResult(result);
    if (merged) {
      setMergedPointCloud(merged);
      setViewMode("merged");
    }
  };

  const handleClearTarget = () => {
    setTargetPointCloud(null);
    setTargetPointCount(0);
    setMergedPointCloud(null);
    setRegistrationResult(null);
    setViewMode("source");
  };

  const handleClearRegistration = () => {
    setRegistrationResult(null);
    setMergedPointCloud(null);
    setViewMode("source");
  };

  const handleApplyHeightColoring = async () => {
    try {
      const cloud = await applyHeightColoring();
      if (cloud) {
        setPointCloud(cloud);
      }
    } catch (e) {
      console.error("Failed to apply height coloring:", e);
    }
  };

  const getDisplayCloud = (): PointCloud | null => {
    switch (viewMode) {
      case "source":
        return pointCloud;
      case "target":
        return targetPointCloud;
      case "merged":
        return mergedPointCloud;
      default:
        return pointCloud;
    }
  };

  const displayCloud = getDisplayCloud();
  const formatPoints = displayCloud?.points.length.toLocaleString() || "0";

  return (
    <div className="app">
      <div className="sidebar">
        <div className="sidebar-header">
          <h1>PointCloud WebRTC</h1>
        </div>

        <div className="sidebar-content">
          {role === "sender" && (
            <FileLoader
              onPointCloudLoaded={handlePointCloudLoaded}
              onLoadingChange={setLoading}
            />
          )}

          {role === "sender" && (
            <DownsamplingControls
              disabled={!pointCloud || loading}
              onDownsample={handleDownsample}
            />
          )}

          {role === "sender" && (
            <RegistrationPanel
              sourcePointCloud={pointCloud}
              targetPointCloud={targetPointCloud}
              registrationResult={registrationResult}
              onTargetPointCloudLoaded={handleTargetPointCloudLoaded}
              onRegistrationComplete={handleRegistrationComplete}
              onClearTarget={handleClearTarget}
              onClearRegistration={handleClearRegistration}
            />
          )}

          <ConnectionPanel role={role} onRoleChange={setRole}
          />

          {role === "sender" && (
            <TransferControls
              role="sender"
              hasPointCloud={!!pointCloud}
              onPointCloudReceived={handlePointCloudReceived}
              onCompressionComplete={() => {}}
            />
          )}

          {role === "receiver" && (
            <TransferControls
              role="receiver"
              hasPointCloud={!!pointCloud}
              onPointCloudReceived={handlePointCloudReceived}
              onCompressionComplete={() => {}}
            />
          )}
        </div>

        <div className="sidebar-footer">
          {displayCloud && (
            <div className="file-info">
              <div className="file-info-row">
                <span className="file-info-label">View:</span>
                <span className="file-info-value">
                  {viewMode === "source" && "Source"}
                  {viewMode === "target" && "Target"}
                  {viewMode === "merged" && "Merged"}
                </span>
              </div>
              <div className="file-info-row">
                <span className="file-info-label">Points:</span>
                <span className="file-info-value">{formatPoints}</span>
              </div>
              {originalCount > 0 && viewMode === "source" && (
                <div className="file-info-row">
                  <span className="file-info-label">Original:</span>
                  <span className="file-info-value">
                    {originalCount.toLocaleString()}
                  </span>
                </div>
              )}
            </div>
          )}
        </div>
      </div>

      <div className="viewer-container">
        <PointCloudViewer pointCloud={displayCloud} colorMode={colorMode} />

        {displayCloud && (
          <>
            <div className="overlay">
              <div style={{ marginBottom: 8, fontWeight: 600 }}>
                Point Cloud Info
              </div>
              <div style={{ fontSize: 11, color: "#aaa" }}>
                Points: {displayCloud.points.length.toLocaleString()}
              </div>
              <div style={{ fontSize: 11, color: "#aaa" }}>
                Bounds: [
                {displayCloud.bounds.min.x.toFixed(2)},{" "}
                {displayCloud.bounds.min.y.toFixed(2)},{" "}
                {displayCloud.bounds.min.z.toFixed(2)}]
              </div>
              <div style={{ fontSize: 11, color: "#aaa", marginTop: 4 }}>
                to [
                {displayCloud.bounds.max.x.toFixed(2)},{" "}
                {displayCloud.bounds.max.y.toFixed(2)},{" "}
                {displayCloud.bounds.max.z.toFixed(2)}]
              </div>
            </div>

            <div className="overlay-right">
              <div style={{ marginBottom: 8, fontWeight: 600 }}>
                View Controls
              </div>
              {targetPointCloud && (
                <div className="tabs" style={{ marginBottom: 8 }}>
                  <div
                    className={`tab ${viewMode === "source" ? "active" : ""}`}
                    onClick={() => setViewMode("source")}
                    style={{ fontSize: 10, padding: "6px 8px" }}
                  >
                    Source
                  </div>
                  <div
                    className={`tab ${viewMode === "target" ? "active" : ""}`}
                    onClick={() => setViewMode("target")}
                    style={{ fontSize: 10, padding: "6px 8px" }}
                  >
                    Target
                  </div>
                  {mergedPointCloud && (
                    <div
                      className={`tab ${viewMode === "merged" ? "active" : ""}`}
                      onClick={() => setViewMode("merged")}
                      style={{ fontSize: 10, padding: "6px 8px" }}
                    >
                      Merged
                    </div>
                  )}
                </div>
              )}
              <div className="tabs" style={{ marginBottom: 8 }}>
                <div
                  className={`tab ${colorMode === "height" ? "active" : ""}`}
                  onClick={() => setColorMode("height")}
                  style={{ fontSize: 10, padding: "6px 8px" }}
                >
                  Height
                </div>
                <div
                  className={`tab ${colorMode === "original" ? "active" : ""}`}
                  onClick={() => setColorMode("original")}
                  style={{ fontSize: 10, padding: "6px 8px" }}
                >
                  Original
                </div>
              </div>
              <button
                className="btn btn-secondary"
                onClick={handleApplyHeightColoring}
                style={{ fontSize: 11, padding: "6px 12px" }}
              >
                Apply Height Colors
              </button>
              <div style={{ marginTop: 8, fontSize: 10, color: "#888" }}>
                Mouse drag: Rotate
              </div>
              <div style={{ fontSize: 10, color: "#888" }}>
                Scroll: Zoom
              </div>
            </div>
          </>
        )}

        {registrationResult && (
          <RegistrationToolbar
            registrationResult={registrationResult}
            sourcePointCount={originalCount || 0}
            targetPointCount={targetPointCount}
            hasMergedCloud={!!mergedPointCloud}
          />
        )}

        {!displayCloud && (
          <div
            style={{
              position: "absolute",
              top: "50%",
              left: "50%",
              transform: "translate(-50%, -50%)",
              textAlign: "center",
              color: "#888",
            }}
          >
            <div style={{ fontSize: 48, marginBottom: 16 }}>📊</div>
            <div style={{ fontSize: 16 }}>
              {role === "sender"
                ? "Load a PLY file to visualize"
                : "Receive point cloud data to visualize"}
            </div>
            <div style={{ fontSize: 12, marginTop: 8, color: "#666" }}>
              {role === "sender"
                ? "Use the panel to select and load a PLY file"
                : "Use the panel to receive point cloud data"}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
