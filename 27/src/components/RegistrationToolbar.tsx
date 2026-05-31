import type { RegistrationResult } from "../types";

interface RegistrationToolbarProps {
  registrationResult: RegistrationResult | null;
  sourcePointCount: number;
  targetPointCount: number;
  hasMergedCloud: boolean;
}

export function RegistrationToolbar({
  registrationResult,
  sourcePointCount,
  targetPointCount,
  hasMergedCloud,
}: RegistrationToolbarProps) {
  if (!registrationResult) return null;

  const rmse = registrationResult.rmse;
  const rmseColor =
    rmse < 0.01
      ? "#2ed573"
      : rmse < 0.1
      ? "#ffa502"
      : "#ff4757";

  const formatMatrix = (data: number[][]) => {
    return data
      .map((row) =>
        row
          .map((v) => (Math.abs(v) < 0.0001 && v !== 0 ? v.toExponential(2) : v.toFixed(4)))
          .join("\t")
      )
      .join("\n");
  };

  return (
    <div
      style={{
        position: "absolute",
        top: 16,
        left: 16,
        background: "rgba(22, 33, 62, 0.95)",
        padding: 16,
        borderRadius: 8,
        border: "1px solid #0f3460",
        fontSize: 12,
        backdropFilter: "blur(10px)",
        minWidth: 240,
        zIndex: 100,
      }}
    >
      <div
        style={{
          marginBottom: 12,
          fontWeight: 600,
          color: "#e94560",
          fontSize: 14,
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
        }}
      >
        <span>Registration Result</span>
        <span
          style={{
            fontSize: 10,
            padding: "2px 8px",
            borderRadius: 10,
            background: registrationResult.converged
              ? "rgba(46, 213, 115, 0.2)"
              : "rgba(255, 165, 2, 0.2)",
            color: registrationResult.converged ? "#2ed573" : "#ffa502",
          }}
        >
          {registrationResult.converged ? "Converged" : "Did not converge"}
        </span>
      </div>

      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 8, marginBottom: 12 }}>
        <div>
          <div style={{ fontSize: 10, color: "#888", textTransform: "uppercase" }}>RMSE</div>
          <div style={{ fontSize: 18, fontWeight: 600, color: rmseColor }}>
            {rmse.toFixed(6)}
          </div>
        </div>
        <div>
          <div style={{ fontSize: 10, color: "#888", textTransform: "uppercase" }}>Iterations</div>
          <div style={{ fontSize: 18, fontWeight: 600, color: "#eaeaea" }}>
            {registrationResult.iterations}
          </div>
        </div>
        <div>
          <div style={{ fontSize: 10, color: "#888", textTransform: "uppercase" }}>Fitness</div>
          <div style={{ fontSize: 18, fontWeight: 600, color: "#eaeaea" }}>
            {(registrationResult.fitness * 100).toFixed(1)}%
          </div>
        </div>
        <div>
          <div style={{ fontSize: 10, color: "#888", textTransform: "uppercase" }}>Inliers</div>
          <div style={{ fontSize: 18, fontWeight: 600, color: "#eaeaea" }}>
            {registrationResult.correspondence_count}
          </div>
        </div>
      </div>

      <div
        style={{
          borderTop: "1px solid #0f3460",
          paddingTop: 12,
          marginTop: 12,
        }}
      >
        <div
          style={{
            fontSize: 10,
            color: "#888",
            textTransform: "uppercase",
            marginBottom: 8,
          }}
        >
          Transformation Matrix
        </div>
        <pre
          style={{
            fontSize: 10,
            color: "#aaa",
            background: "#1a1a2e",
            padding: 8,
            borderRadius: 4,
            margin: 0,
            overflowX: "auto",
            fontFamily: "monospace",
          }}
        >
          {formatMatrix(registrationResult.transformation.data)}
        </pre>
      </div>

      {(sourcePointCount > 0 || targetPointCount > 0) && (
        <div
          style={{
            borderTop: "1px solid #0f3460",
            paddingTop: 12,
            marginTop: 12,
            display: "flex",
            justifyContent: "space-between",
            fontSize: 10,
            color: "#888",
          }}
        >
          <span>Source: {sourcePointCount.toLocaleString()} pts</span>
          <span>Target: {targetPointCount.toLocaleString()} pts</span>
        </div>
      )}

      {hasMergedCloud && (
        <div
          style={{
            marginTop: 12,
            padding: 8,
            background: "rgba(46, 213, 115, 0.1)",
            borderRadius: 4,
            textAlign: "center",
            fontSize: 11,
            color: "#2ed573",
          }}
        >
          Merged point cloud loaded
        </div>
      )}
    </div>
  );
}
