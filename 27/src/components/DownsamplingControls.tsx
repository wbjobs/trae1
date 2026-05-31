import { useState } from "react";
import { downsamplePointCloud, getVoxelSizeForTarget } from "../tauriCommands";
import type { PointCloud, DownsampleResponse } from "../types";

interface DownsamplingControlsProps {
  disabled: boolean;
  onDownsample: (cloud: PointCloud, originalCount: number, downsampledCount: number) => void;
}

type DownsampleMode = "voxel" | "target";

export function DownsamplingControls({ disabled, onDownsample }: DownsamplingControlsProps) {
  const [mode, setMode] = useState<DownsampleMode>("voxel");
  const [voxelSize, setVoxelSize] = useState<number>(0.01);
  const [targetPoints, setTargetPoints] = useState<number>(100000);
  const [processing, setProcessing] = useState<boolean>(false);
  const [error, setError] = useState<string>("");

  const handleAutoVoxelSize = async () => {
    try {
      const size = await getVoxelSizeForTarget(targetPoints);
      if (size > 0) {
        setVoxelSize(parseFloat(size.toFixed(4)));
      }
    } catch (e) {
      console.error("Failed to compute voxel size:", e);
    }
  };

  const handleDownsample = async () => {
    setProcessing(true);
    setError("");

    try {
      const request = mode === "voxel"
        ? { voxel_size: voxelSize }
        : { voxel_size: 0.01, target_points: targetPoints };

      const response: DownsampleResponse = await downsamplePointCloud(request);

      if (response.success && response.point_cloud) {
        onDownsample(response.point_cloud, response.original_count, response.downsampled_count);
      } else {
        setError(response.error || "Failed to downsample");
      }
    } catch (e) {
      setError(`Error: ${e}`);
    } finally {
      setProcessing(false);
    }
  };

  return (
    <div className="section">
      <div className="section-title">Downsampling</div>

      <div className="tabs">
        <div
          className={`tab ${mode === "voxel" ? "active" : ""}`}
          onClick={() => setMode("voxel")}
        >
          Voxel Size
        </div>
        <div
          className={`tab ${mode === "target" ? "active" : ""}`}
          onClick={() => setMode("target")}
        >
          Target Points
        </div>
      </div>

      {mode === "voxel" && (
        <>
          <div className="input-group">
            <label>Voxel Size</label>
            <input
              type="number"
              step="0.001"
              min="0.001"
              value={voxelSize}
              onChange={(e) => setVoxelSize(parseFloat(e.target.value) || 0.01)}
              disabled={disabled || processing}
            />
          </div>
          <button
            className="btn btn-secondary"
            onClick={handleAutoVoxelSize}
            disabled={disabled || processing}
            style={{ fontSize: 12, padding: "6px 12px" }}
          >
            Auto Calculate
          </button>
        </>
      )}

      {mode === "target" && (
        <div className="input-group">
          <label>Target Points</label>
          <input
            type="number"
            min="100"
            step="1000"
            value={targetPoints}
            onChange={(e) => setTargetPoints(parseInt(e.target.value) || 100000)}
            disabled={disabled || processing}
          />
        </div>
      )}

      <button
        className="btn btn-secondary"
        onClick={handleDownsample}
        disabled={disabled || processing}
        style={{ marginTop: 12 }}
      >
        {processing ? "Processing..." : "Apply Downsampling"}
      </button>

      {error && (
        <div style={{ marginTop: 8, padding: 8, background: "rgba(255, 71, 87, 0.2)", borderRadius: 4, fontSize: 12, color: "#ff4757" }}>
          {error}
        </div>
      )}
    </div>
  );
}
