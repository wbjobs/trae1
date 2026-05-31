import { useState } from "react";
import { loadPlyFile } from "../tauriCommands";
import type { PointCloud, LoadPlyResponse } from "../types";
import { open } from "@tauri-apps/plugin-dialog";

interface FileLoaderProps {
  onPointCloudLoaded: (cloud: PointCloud, originalCount: number) => void;
  onLoadingChange: (loading: boolean) => void;
}

export function FileLoader({ onPointCloudLoaded, onLoadingChange }: FileLoaderProps) {
  const [filePath, setFilePath] = useState<string>("");
  const [fileName, setFileName] = useState<string>("");
  const [pointCount, setPointCount] = useState<number>(0);
  const [error, setError] = useState<string>("");
  const [loading, setLoading] = useState<boolean>(false);

  const handleSelectFile = async () => {
    try {
      const selected = await open({
        multiple: false,
        filters: [
          {
            name: "PLY Files",
            extensions: ["ply"],
          },
        ],
      });

      if (selected && !Array.isArray(selected)) {
        setFilePath(selected);
        setFileName(selected.split("\\").pop() || selected.split("/").pop() || selected);
        setError("");
      }
    } catch (e) {
      setError(`Failed to select file: ${e}`);
    }
  };

  const handleLoadFile = async () => {
    if (!filePath) {
      setError("Please select a file first");
      return;
    }

    setLoading(true);
    onLoadingChange(true);
    setError("");

    try {
      const response: LoadPlyResponse = await loadPlyFile(filePath);

      if (response.success && response.point_cloud) {
        setPointCount(response.processed_count);
        onPointCloudLoaded(response.point_cloud, response.original_count);
      } else {
        setError(response.error || "Failed to load file");
      }
    } catch (e) {
      setError(`Error loading file: ${e}`);
    } finally {
      setLoading(false);
      onLoadingChange(false);
    }
  };

  return (
    <div className="section">
      <div className="section-title">File Loader</div>

      <button className="btn btn-secondary" onClick={handleSelectFile} disabled={loading}>
        Select PLY File
      </button>

      {fileName && (
        <div className="file-info" style={{ marginTop: 12 }}>
          <div className="file-info-row">
            <span className="file-info-label">File:</span>
            <span className="file-info-value" style={{ overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap", maxWidth: 180 }}>
              {fileName}
            </span>
          </div>
          {pointCount > 0 && (
            <div className="file-info-row">
              <span className="file-info-label">Points:</span>
              <span className="file-info-value">{pointCount.toLocaleString()}</span>
            </div>
          )}
        </div>
      )}

      {filePath && !loading && (
        <button
          className="btn btn-primary"
          onClick={handleLoadFile}
          style={{ marginTop: 8 }}
        >
          Load Point Cloud
        </button>
      )}

      {loading && (
        <div style={{ marginTop: 12, textAlign: "center", color: "#e94560" }}>
          Loading...
        </div>
      )}

      {error && (
        <div style={{ marginTop: 12, padding: 8, background: "rgba(255, 71, 87, 0.2)", borderRadius: 4, fontSize: 12, color: "#ff4757" }}>
          {error}
        </div>
      )}

      <div style={{ marginTop: 12, fontSize: 11, color: "#888" }}>
        Max supported: 5,000,000 points
      </div>
    </div>
  );
}
