import { useState } from "react";
import {
  loadTargetPlyFile,
  runICPRegistration,
  runManualRegistration,
  clearTargetPointCloud,
  clearRegistration,
  getAlignedSourcePointCloud,
  getMergedPointCloud,
} from "../tauriCommands";
import type {
  PointCloud,
  RegistrationResult,
  ManualCorrespondence,
  ICPRegistrationRequest,
} from "../types";
import { open } from "@tauri-apps/plugin-dialog";

interface RegistrationPanelProps {
  sourcePointCloud: PointCloud | null;
  targetPointCloud: PointCloud | null;
  registrationResult: RegistrationResult | null;
  onTargetPointCloudLoaded: (cloud: PointCloud, pointCount: number) => void;
  onRegistrationComplete: (result: RegistrationResult, mergedCloud: PointCloud | null) => void;
  onClearTarget: () => void;
  onClearRegistration: () => void;
}

type RegistrationMode = "manual" | "icp";

export function RegistrationPanel({
  sourcePointCloud,
  targetPointCloud,
  registrationResult,
  onTargetPointCloudLoaded,
  onRegistrationComplete,
  onClearTarget,
  onClearRegistration,
}: RegistrationPanelProps) {
  const [mode, setMode] = useState<RegistrationMode>("icp");
  const [targetFilePath, setTargetFilePath] = useState<string>("");
  const [targetFileName, setTargetFileName] = useState<string>("");
  const [targetPointCount, setTargetPointCount] = useState<number>(0);
  const [loading, setLoading] = useState<boolean>(false);
  const [processing, setProcessing] = useState<boolean>(false);
  const [error, setError] = useState<string>("");
  const [correspondences, setCorrespondences] = useState<ManualCorrespondence[]>([]);
  const [sourcePickMode, setSourcePickMode] = useState<boolean>(false);
  const [targetPickMode, setTargetPickMode] = useState<boolean>(false);
  const [pendingSourcePoint, setPendingSourcePoint] = useState<[number, number, number] | null>(null);

  const [maxIterations, setMaxIterations] = useState<number>(100);
  const [tolerance, setTolerance] = useState<number>(1e-8);
  const [useRobustKernel, setUseRobustKernel] = useState<boolean>(true);
  const [robustThreshold, setRobustThreshold] = useState<number>(0.5);

  const handleSelectTargetFile = async () => {
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
        setTargetFilePath(selected);
        setTargetFileName(
          selected.split("\\").pop() || selected.split("/").pop() || selected
        );
        setError("");

        setLoading(true);
        const response = await loadTargetPlyFile(selected);

        if (response.success && response.point_cloud) {
          setTargetPointCount(response.point_count);
          onTargetPointCloudLoaded(response.point_cloud, response.point_count);
        } else {
          setError(response.error || "Failed to load target point cloud");
        }
        setLoading(false);
      }
    } catch (e) {
      setError(`Failed to select file: ${e}`);
      setLoading(false);
    }
  };

  const handleRunICP = async () => {
    if (!sourcePointCloud || !targetPointCloud) {
      setError("Both source and target point clouds are required");
      return;
    }

    setProcessing(true);
    setError("");

    try {
      const request: ICPRegistrationRequest = {
        max_iterations: maxIterations,
        tolerance,
        use_robust_kernel: useRobustKernel,
        robust_kernel_threshold: robustThreshold,
      };

      const result = await runICPRegistration(request);

      if (result) {
        const aligned = await getAlignedSourcePointCloud();
        const merged = await getMergedPointCloud();
        onRegistrationComplete(result, merged || aligned);
      } else {
        setError("ICP registration failed");
      }
    } catch (e) {
      setError(`ICP registration error: ${e}`);
    } finally {
      setProcessing(false);
    }
  };

  const handleRunManualRegistration = async () => {
    if (correspondences.length < 3) {
      setError("At least 3 correspondences required");
      return;
    }

    setProcessing(true);
    setError("");

    try {
      const result = await runManualRegistration(correspondences);

      if (result) {
        const aligned = await getAlignedSourcePointCloud();
        const merged = await getMergedPointCloud();
        onRegistrationComplete(result, merged || aligned);
      } else {
        setError("Manual registration failed");
      }
    } catch (e) {
      setError(`Manual registration error: ${e}`);
    } finally {
      setProcessing(false);
    }
  };

  const handleAddCorrespondence = () => {
    if (pendingSourcePoint) {
      setCorrespondences([
        ...correspondences,
        {
          source_point: pendingSourcePoint,
          target_point: [0, 0, 0],
        },
      ]);
      setPendingSourcePoint(null);
      setSourcePickMode(false);
      setTargetPickMode(true);
    }
  };

  const handleRemoveCorrespondence = (index: number) => {
    setCorrespondences(correspondences.filter((_, i) => i !== index));
  };

  const handleClearCorrespondences = () => {
    setCorrespondences([]);
    setPendingSourcePoint(null);
    setSourcePickMode(false);
    setTargetPickMode(false);
  };

  const handleClearTarget = async () => {
    await clearTargetPointCloud();
    setTargetFilePath("");
    setTargetFileName("");
    setTargetPointCount(0);
    setCorrespondences([]);
    onClearTarget();
  };

  const handleClearRegistration = async () => {
    await clearRegistration();
    onClearRegistration();
  };

  return (
    <div className="section">
      <div className="section-title">Point Cloud Registration</div>

      <div className="tabs">
        <div
          className={`tab ${mode === "icp" ? "active" : ""}`}
          onClick={() => setMode("icp")}
        >
          ICP
        </div>
        <div
          className={`tab ${mode === "manual" ? "active" : ""}`}
          onClick={() => setMode("manual")}
        >
          Manual
        </div>
      </div>

      {!targetPointCloud ? (
        <>
          <button
            className="btn btn-secondary"
            onClick={handleSelectTargetFile}
            disabled={loading}
          >
            {loading ? "Loading..." : "Select Target PLY File"}
          </button>

          {targetFileName && (
            <div className="file-info" style={{ marginTop: 12 }}>
              <div className="file-info-row">
                <span className="file-info-label">File:</span>
                <span
                  className="file-info-value"
                  style={{
                    overflow: "hidden",
                    textOverflow: "ellipsis",
                    whiteSpace: "nowrap",
                    maxWidth: 180,
                  }}
                >
                  {targetFileName}
                </span>
              </div>
              {targetPointCount > 0 && (
                <div className="file-info-row">
                  <span className="file-info-label">Points:</span>
                  <span className="file-info-value">
                    {targetPointCount.toLocaleString()}
                  </span>
                </div>
              )}
            </div>
          )}
        </>
      ) : (
        <>
          <div className="file-info">
            <div className="file-info-row">
              <span className="file-info-label">Target:</span>
              <span
                className="file-info-value"
                style={{
                  overflow: "hidden",
                  textOverflow: "ellipsis",
                  whiteSpace: "nowrap",
                  maxWidth: 150,
                }}
              >
                {targetFileName}
              </span>
            </div>
            <div className="file-info-row">
              <span className="file-info-label">Points:</span>
              <span className="file-info-value">
                {targetPointCount.toLocaleString()}
              </span>
            </div>
          </div>

          {mode === "icp" && (
            <>
              <div className="input-group" style={{ marginTop: 12 }}>
                <label>Max Iterations</label>
                <input
                  type="number"
                  min="10"
                  max="1000"
                  step="10"
                  value={maxIterations}
                  onChange={(e) =>
                    setMaxIterations(parseInt(e.target.value) || 100)
                  }
                  disabled={processing}
                />
              </div>

              <div className="input-group">
                <label>Tolerance</label>
                <input
                  type="number"
                  min="1e-12"
                  max="1e-4"
                  step="1e-10"
                  value={tolerance}
                  onChange={(e) =>
                    setTolerance(parseFloat(e.target.value) || 1e-8)
                  }
                  disabled={processing}
                />
              </div>

              <div className="input-group">
                <label>
                  <input
                    type="checkbox"
                    checked={useRobustKernel}
                    onChange={(e) => setUseRobustKernel(e.target.checked)}
                    disabled={processing}
                    style={{ marginRight: 8 }}
                  />
                  Use Robust Kernel
                </label>
              </div>

              {useRobustKernel && (
                <div className="input-group">
                  <label>Robust Threshold</label>
                  <input
                    type="number"
                    min="0.1"
                    max="2.0"
                    step="0.1"
                    value={robustThreshold}
                    onChange={(e) =>
                      setRobustThreshold(parseFloat(e.target.value) || 0.5)
                    }
                    disabled={processing}
                  />
                </div>
              )}

              <button
                className="btn btn-primary"
                onClick={handleRunICP}
                disabled={!sourcePointCloud || !targetPointCloud || processing}
                style={{ marginTop: 12 }}
              >
                {processing ? "Running ICP..." : "Run ICP Registration"}
              </button>
            </>
          )}

          {mode === "manual" && (
            <>
              <div style={{ marginTop: 12, marginBottom: 12 }}>
                <div
                  style={{
                    fontSize: 11,
                    color: "#aaa",
                    marginBottom: 8,
                  }}
                >
                  Correspondences: {correspondences.length}/3+
                </div>

                {correspondences.length > 0 && (
                  <div
                    style={{
                      maxHeight: 120,
                      overflowY: "auto",
                      background: "#1a1a2e",
                      borderRadius: 4,
                      padding: 8,
                    }}
                  >
                    {correspondences.map((c, i) => (
                      <div
                        key={i}
                        style={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          padding: "4px 0",
                          borderBottom:
                            i < correspondences.length - 1
                              ? "1px solid #0f3460"
                              : "none",
                          fontSize: 10,
                        }}
                      >
                        <span style={{ color: "#888" }}>Pair {i + 1}</span>
                        <span style={{ color: "#e94560", cursor: "pointer" }}>
                          ✕
                        </span>
                      </div>
                    ))}
                  </div>
                )}

                <button
                  className="btn btn-secondary"
                  onClick={handleClearCorrespondences}
                  disabled={correspondences.length === 0}
                  style={{ marginTop: 8, fontSize: 11, padding: "4px 8px" }}
                >
                  Clear All
                </button>
              </div>

              <button
                className="btn btn-primary"
                onClick={handleRunManualRegistration}
                disabled={correspondences.length < 3 || processing}
              >
                {processing
                  ? "Processing..."
                  : `Run Manual Registration (${correspondences.length} pairs)`}
              </button>
            </>
          )}

          {registrationResult && (
            <div
              style={{
                marginTop: 12,
                padding: 8,
                background: "rgba(46, 213, 115, 0.1)",
                borderRadius: 4,
                fontSize: 11,
              }}
            >
              <div style={{ color: "#2ed573", fontWeight: 600 }}>
                Registration Complete!
              </div>
              <div style={{ color: "#aaa", marginTop: 4 }}>
                RMSE: {registrationResult.rmse.toFixed(6)}
              </div>
              <div style={{ color: "#aaa" }}>
                Iterations: {registrationResult.iterations}
              </div>
              <div style={{ color: "#aaa" }}>
                Fitness: {(registrationResult.fitness * 100).toFixed(1)}%
              </div>
            </div>
          )}

          <div className="controls-grid" style={{ marginTop: 12 }}>
            <button
              className="btn btn-secondary"
              onClick={handleClearRegistration}
              disabled={!registrationResult}
              style={{ fontSize: 11, padding: "4px 8px" }}
            >
              Clear Result
            </button>
            <button
              className="btn btn-secondary"
              onClick={handleClearTarget}
              style={{ fontSize: 11, padding: "4px 8px" }}
            >
              Remove Target
            </button>
          </div>
        </>
      )}

      {error && (
        <div
          style={{
            marginTop: 8,
            padding: 8,
            background: "rgba(255, 71, 87, 0.2)",
            borderRadius: 4,
            fontSize: 12,
            color: "#ff4757",
          }}
        >
          {error}
        </div>
      )}
    </div>
  );
}
