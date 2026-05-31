import { useState } from "react";
import { compressCurrentPointCloud, decompressPointCloudData } from "../tauriCommands";
import type { PointCloud, CompressResponse, DecompressResponse } from "../types";

interface TransferControlsProps {
  role: "sender" | "receiver";
  hasPointCloud: boolean;
  onPointCloudReceived: (cloud: PointCloud) => void;
  onCompressionComplete: (data: number[], ratio: number) => void;
}

export function TransferControls({
  role,
  hasPointCloud,
  onPointCloudReceived,
  onCompressionComplete,
}: TransferControlsProps) {
  const [compressing, setCompressing] = useState<boolean>(false);
  const [decompressing, setDecompressing] = useState<boolean>(false);
  const [compressedData, setCompressedData] = useState<number[] | null>(null);
  const [compressionRatio, setCompressionRatio] = useState<number>(0);
  const [originalSize, setOriginalSize] = useState<number>(0);
  const [compressedSize, setCompressedSize] = useState<number>(0);
  const [receivedData, setReceivedData] = useState<string>("");
  const [error, setError] = useState<string>("");

  const handleCompress = async () => {
    setCompressing(true);
    setError("");

    try {
      const response: CompressResponse = await compressCurrentPointCloud();

      if (response.success && response.data) {
        setCompressedData(response.data);
        setCompressionRatio(response.ratio);
        setOriginalSize(response.original_size);
        setCompressedSize(response.compressed_size);
        onCompressionComplete(response.data, response.ratio);
      } else {
        setError(response.error || "Compression failed");
      }
    } catch (e) {
      setError(`Error: ${e}`);
    } finally {
      setCompressing(false);
    }
  };

  const handleDecompress = async () => {
    if (!receivedData) {
      setError("Please enter compressed data");
      return;
    }

    setDecompressing(true);
    setError("");

    try {
      const dataArray: number[] = JSON.parse(receivedData);
      const response: DecompressResponse = await decompressPointCloudData(dataArray);

      if (response.success && response.point_cloud) {
        onPointCloudReceived(response.point_cloud);
      } else {
        setError(response.error || "Decompression failed");
      }
    } catch (e) {
      setError(`Error: ${e}`);
    } finally {
      setDecompressing(false);
    }
  };

  const formatBytes = (bytes: number): string => {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
    return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
  };

  return (
    <div className="section">
      <div className="section-title">
        {role === "sender" ? "Send Point Cloud" : "Receive Point Cloud"}
      </div>

      {role === "sender" && (
        <>
          <button
            className="btn btn-primary"
            onClick={handleCompress}
            disabled={!hasPointCloud || compressing}
          >
            {compressing ? "Compressing..." : "Compress & Prepare"}
          </button>

          {compressedData && (
            <div className="file-info" style={{ marginTop: 12 }}>
              <div className="file-info-row">
                <span className="file-info-label">Original:</span>
                <span className="file-info-value">{formatBytes(originalSize)}</span>
              </div>
              <div className="file-info-row">
                <span className="file-info-label">Compressed:</span>
                <span className="file-info-value">{formatBytes(compressedSize)}</span>
              </div>
              <div className="file-info-row">
                <span className="file-info-label">Ratio:</span>
                <span className="file-info-value" style={{ color: compressionRatio < 1 ? "#2ed573" : "#ff4757" }}>
                  {(compressionRatio * 100).toFixed(1)}%
                </span>
              </div>
              <div className="file-info-row">
                <span className="file-info-label">Chunks:</span>
                <span className="file-info-value">{Math.ceil(compressedData.length / 60000)}</span>
              </div>
            </div>
          )}

          {compressedData && (
            <div className="input-group" style={{ marginTop: 12 }}>
              <label>Compressed Data (Base64)</label>
              <textarea
                style={{
                  width: "100%",
                  height: 80,
                  background: "#1a1a2e",
                  border: "1px solid #0f3460",
                  borderRadius: 6,
                  color: "#eaeaea",
                  fontSize: 11,
                  padding: 8,
                  resize: "none",
                }}
                value={btoa(String.fromCharCode(...compressedData.slice(0, 500)))}
                readOnly
              />
              <div style={{ fontSize: 10, color: "#888", marginTop: 4 }}>
                (Showing first 500 bytes preview)
              </div>
            </div>
          )}
        </>
      )}

      {role === "receiver" && (
        <>
          <div className="input-group">
            <label>Received Data (JSON Array)</label>
            <textarea
              style={{
                width: "100%",
                height: 100,
                background: "#1a1a2e",
                border: "1px solid #0f3460",
                borderRadius: 6,
                color: "#eaeaea",
                fontSize: 11,
                padding: 8,
                resize: "none",
              }}
              value={receivedData}
              onChange={(e) => setReceivedData(e.target.value)}
              placeholder='Paste compressed data as JSON array...'
            />
          </div>

          <button
            className="btn btn-primary"
            onClick={handleDecompress}
            disabled={!receivedData || decompressing}
          >
            {decompressing ? "Decompressing..." : "Decompress & Display"}
          </button>
        </>
      )}

      {error && (
        <div style={{ marginTop: 8, padding: 8, background: "rgba(255, 71, 87, 0.2)", borderRadius: 4, fontSize: 12, color: "#ff4757" }}>
          {error}
        </div>
      )}
    </div>
  );
}
