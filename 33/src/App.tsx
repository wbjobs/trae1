import { useCallback, useEffect, useRef, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { displayLabel } from "./labels";

type ModelStatus = {
  loaded: boolean;
  path: string | null;
  input_shape: number[] | null;
  labels: string[];
  error: string | null;
};

type WSResult = {
  type: "result";
  label: string;
  label_index: number;
  confidence: number;
  probabilities: number[];
  inference_ms: number;
  total_ms: number;
  timestamp: number;
};

type WSError = {
  type: "error";
  message: string;
};

type WSStats = {
  type: "stats";
  fps: number;
  avg_inference_ms: number;
  queue_depth: number;
};

type WSStatus = {
  type: "status";
  model_loaded: boolean;
  port: number;
};

type WSMessage = WSResult | WSError | WSStats | WSStatus;

type RecordingItem = {
  filename: string;
  path: string;
  size_bytes: number;
  created_at: string;
};

const INFERENCE_INTERVAL_MS = 80; // ~12.5 fps to backend, stays under 50ms per-frame inference

function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

function formatTime(seconds: number): string {
  const s = Math.floor(seconds);
  const mm = Math.floor(s / 60).toString().padStart(2, "0");
  const ss = (s % 60).toString().padStart(2, "0");
  return `${mm}:${ss}`;
}

export default function App() {
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const overlayRef = useRef<HTMLCanvasElement | null>(null);
  const wsRef = useRef<WebSocket | null>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const recorderRef = useRef<MediaRecorder | null>(null);
  const recordedChunksRef = useRef<Blob[]>([]);
  const lastSendRef = useRef<number>(0);
  const recordingStartRef = useRef<number>(0);

  const [cameraActive, setCameraActive] = useState(false);
  const [wsConnected, setWsConnected] = useState(false);
  const [wsPort, setWsPort] = useState<number | null>(null);
  const [modelStatus, setModelStatus] = useState<ModelStatus | null>(null);
  const [latestResult, setLatestResult] = useState<WSResult | null>(null);
  const [stats, setStats] = useState<WSStats | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [cameraError, setCameraError] = useState<string | null>(null);

  const [isRecording, setIsRecording] = useState(false);
  const [recordingElapsed, setRecordingElapsed] = useState(0);
  const [recordings, setRecordings] = useState<RecordingItem[]>([]);
  const [preferredDevice, setPreferredDevice] = useState<string>("");
  const [devices, setDevices] = useState<MediaDeviceInfo[]>([]);

  // Load initial model status
  useEffect(() => {
    invoke<ModelStatus>("get_model_status")
      .then(setModelStatus)
      .catch(() => {});
  }, []);

  // Get WebSocket port from backend via event or invoke
  useEffect(() => {
    let cancelled = false;

    (async () => {
      const unlisten = await listen<number>("ws-ready", (evt) => {
        if (!cancelled) {
          setWsPort(evt.payload);
        }
      });
      // Also poll as a fallback
      try {
        const port = await invoke<number>("get_ws_port");
        if (!cancelled && port) setWsPort(port);
      } catch {
        /* ignore */
      }
      return () => {
        cancelled = true;
        unlisten();
      };
    })();
  }, []);

  // Connect WebSocket once the port is known
  useEffect(() => {
    if (!wsPort) return;
    const url = `ws://127.0.0.1:${wsPort}`;
    const ws = new WebSocket(url);
    wsRef.current = ws;
    ws.binaryType = "arraybuffer";

    ws.onopen = () => {
      setWsConnected(true);
      setError(null);
    };
    ws.onclose = () => {
      setWsConnected(false);
    };
    ws.onerror = () => {
      setError("WebSocket 连接失败");
      setWsConnected(false);
    };
    ws.onmessage = (evt) => {
      let data: WSMessage;
      try {
        if (typeof evt.data === "string") {
          data = JSON.parse(evt.data);
        } else {
          data = JSON.parse(new TextDecoder().decode(evt.data as ArrayBuffer));
        }
      } catch {
        return;
      }
      switch (data.type) {
        case "result":
          setLatestResult(data);
          break;
        case "stats":
          setStats(data);
          break;
        case "status":
          setWsPort(data.port);
          setModelStatus((prev) => (prev ? { ...prev, loaded: data.model_loaded } : prev));
          break;
        case "error":
          setError(data.message);
          break;
      }
    };

    return () => {
      ws.close();
      wsRef.current = null;
      setWsConnected(false);
    };
  }, [wsPort]);

  // Periodically list camera devices
  useEffect(() => {
    let cancelled = false;
    const refresh = async () => {
      try {
        const all = await navigator.mediaDevices.enumerateDevices();
        if (!cancelled) {
          setDevices(all.filter((d) => d.kind === "videoinput"));
        }
      } catch {
        /* ignore */
      }
    };
    refresh();
    const id = setInterval(refresh, 2000);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, []);

  // Start / stop camera
  const startCamera = useCallback(async () => {
    setCameraError(null);
    try {
      const constraints: MediaStreamConstraints = {
        audio: false,
        video: {
          width: { ideal: 960 },
          height: { ideal: 720 },
          ...(preferredDevice ? { deviceId: { exact: preferredDevice } } : {}),
        },
      };
      const stream = await navigator.mediaDevices.getUserMedia(constraints);
      streamRef.current = stream;
      if (videoRef.current) {
        videoRef.current.srcObject = stream;
        await videoRef.current.play().catch(() => {});
      }
      setCameraActive(true);
    } catch (e) {
      console.error(e);
      setCameraError((e as Error).message || "无法访问摄像头");
      setCameraActive(false);
    }
  }, [preferredDevice]);

  const stopCamera = useCallback(() => {
    if (streamRef.current) {
      streamRef.current.getTracks().forEach((t) => t.stop());
      streamRef.current = null;
    }
    if (videoRef.current) {
      videoRef.current.srcObject = null;
    }
    setCameraActive(false);
  }, []);

  useEffect(() => {
    return () => {
      if (streamRef.current) streamRef.current.getTracks().forEach((t) => t.stop());
    };
  }, []);

  // Render loop: paint video to canvas, draw overlay labels, and periodically
  // send frames to the backend via WebSocket.
  useEffect(() => {
    let raf = 0;
    const render = () => {
      const video = videoRef.current;
      const canvas = canvasRef.current;
      const overlay = overlayRef.current;
      if (!video || !canvas || !overlay) {
        raf = requestAnimationFrame(render);
        return;
      }
      if (video.readyState >= 2) {
        const w = video.videoWidth;
        const h = video.videoHeight;
        if (w > 0 && h > 0) {
          if (canvas.width !== w || canvas.height !== h) {
            canvas.width = w;
            canvas.height = h;
          }
          if (overlay.width !== w || overlay.height !== h) {
            overlay.width = w;
            overlay.height = h;
          }
          const ctx = canvas.getContext("2d");
          if (ctx) {
            ctx.drawImage(video, 0, 0, w, h);
          }
          drawOverlay(overlay, latestResult, stats);

          // Periodically capture a JPEG blob and send via WebSocket (fire-and-forget
          // to avoid stalling the render loop on a slow encode).
          const now = performance.now();
          if (
            wsRef.current?.readyState === WebSocket.OPEN &&
            now - lastSendRef.current >= INFERENCE_INTERVAL_MS
          ) {
            lastSendRef.current = now;
            const ws = wsRef.current;
            const snapshot = canvas;
            void new Promise<Blob | null>((resolve) =>
              snapshot.toBlob((b) => resolve(b), "image/jpeg", 0.75)
            ).then(async (blob) => {
              if (!blob) return;
              if (ws.readyState !== WebSocket.OPEN) return;
              const buffer = await blob.arrayBuffer();
              if (ws.readyState === WebSocket.OPEN) {
                ws.send(buffer);
              }
            });
          }
        }
      }
      raf = requestAnimationFrame(render);
    };
    raf = requestAnimationFrame(render);
    return () => cancelAnimationFrame(raf);
  }, [latestResult, stats]);

  // Recording timer
  useEffect(() => {
    if (!isRecording) return;
    const id = setInterval(() => {
      setRecordingElapsed((Date.now() - recordingStartRef.current) / 1000);
    }, 200);
    return () => clearInterval(id);
  }, [isRecording]);

  const startRecording = useCallback(() => {
    if (!canvasRef.current) return;
    try {
      const stream = canvasRef.current.captureStream(30);
      const mime = MediaRecorder.isTypeSupported("video/mp4")
        ? "video/mp4"
        : "video/webm;codecs=vp9";
      const rec = new MediaRecorder(stream, { mimeType: mime, videoBitsPerSecond: 4_000_000 });
      recordedChunksRef.current = [];
      rec.ondataavailable = (e) => {
        if (e.data.size > 0) recordedChunksRef.current.push(e.data);
      };
      rec.onstop = async () => {
        const blob = new Blob(recordedChunksRef.current, {
          type: mime.startsWith("video/mp4") ? "video/mp4" : "video/webm",
        });
        const buffer = await blob.arrayBuffer();
        const base64 = arrayBufferToBase64(buffer);
        const filename = `gesture-${new Date()
          .toISOString()
          .replace(/[:.]/g, "-")}.${mime.startsWith("video/mp4") ? "mp4" : "webm"}`;
        try {
          await invoke<string>("save_recording", {
            filename,
            dataBase64: base64,
          });
          refreshRecordings();
        } catch (e) {
          console.error("Failed to save recording", e);
          setError("保存录制失败：" + (e as Error).message);
        }
      };
      recordingStartRef.current = Date.now();
      recorderRef.current = rec;
      rec.start(250);
      setIsRecording(true);
      setRecordingElapsed(0);
    } catch (e) {
      console.error(e);
      setError("录制失败：" + (e as Error).message);
    }
  }, []);

  const stopRecording = useCallback(() => {
    if (recorderRef.current && recorderRef.current.state !== "inactive") {
      recorderRef.current.stop();
    }
    setIsRecording(false);
  }, []);

  const refreshRecordings = useCallback(async () => {
    try {
      const list = await invoke<RecordingItem[]>("list_recordings");
      setRecordings(list);
    } catch (e) {
      console.error(e);
    }
  }, []);

  const openRecordingFolder = useCallback(() => {
    if (recordings[0]?.path) {
      const dir =
        recordings[0].path.lastIndexOf("\\") > recordings[0].path.lastIndexOf("/")
          ? recordings[0].path.substring(
              0,
              recordings[0].path.lastIndexOf("\\")
            )
          : recordings[0].path.substring(
              0,
              recordings[0].path.lastIndexOf("/")
            );
      invoke("plugin:shell|open", { path: dir }).catch(() => {});
    }
  }, [recordings]);

  // Load model on demand
  const loadModel = useCallback(async () => {
    try {
      const status = await invoke<ModelStatus>("load_model", {});
      setModelStatus(status);
      setError(null);
    } catch (e) {
      setError((e as Error).message);
    }
  }, []);

  const unloadModel = useCallback(async () => {
    try {
      await invoke("unload_model");
      setModelStatus((prev) => (prev ? { ...prev, loaded: false } : prev));
    } catch (e) {
      setError((e as Error).message);
    }
  }, []);

  const canInfer = wsConnected && modelStatus?.loaded && cameraActive;

  return (
    <div className="app">
      <header className="app__header">
        <div className="app__title">🤚 Gesture Recognizer</div>
        <div className="app__status">
          <span>
            模型：
            <b style={{ color: modelStatus?.loaded ? "#34d399" : "#f87171" }}>
              {modelStatus?.loaded ? "已加载" : "未加载"}
            </b>
          </span>
          <span>
            摄像头：
            <b style={{ color: cameraActive ? "#34d399" : "#f87171" }}>
              {cameraActive ? "运行中" : "已关闭"}
            </b>
          </span>
          <span>
            WebSocket：
            <b style={{ color: wsConnected ? "#34d399" : "#f87171" }}>
              {wsConnected ? `已连接 :${wsPort}` : "未连接"}
            </b>
          </span>
          {isRecording && (
            <span>
              <span className="recording-dot" />
              录制中 {formatTime(recordingElapsed)}
            </span>
          )}
        </div>
      </header>

      <aside className="app__sidebar">
        <section className="panel">
          <div className="panel__title">模型</div>
          <div className="panel__row">
            <label>状态</label>
            <input
              readOnly
              value={
                modelStatus?.loaded
                  ? `已加载 · ${modelStatus.input_shape?.join("×") ?? ""}`
                  : "未加载"
              }
            />
          </div>
          <div style={{ display: "flex", gap: 8 }}>
            <button className="btn" onClick={loadModel} disabled={!!modelStatus?.loaded}>
              加载模型
            </button>
            <button
              className="btn btn--ghost"
              onClick={unloadModel}
              disabled={!modelStatus?.loaded}
            >
              卸载
            </button>
          </div>
          <div className="panel__row" style={{ marginTop: 12 }}>
            <label>模型路径</label>
            <input
              readOnly
              value={
                modelStatus?.path
                  ? modelStatus.path
                  : "src-tauri/resources/model.onnx"
              }
            />
          </div>
        </section>

        <section className="panel">
          <div className="panel__title">摄像头</div>
          <div className="panel__row">
            <label>输入设备</label>
            <select
              value={preferredDevice}
              onChange={(e) => setPreferredDevice(e.target.value)}
            >
              <option value="">默认设备</option>
              {devices.map((d) => (
                <option key={d.deviceId} value={d.deviceId}>
                  {d.label || `设备 ${d.deviceId.slice(0, 6)}`}
                </option>
              ))}
            </select>
          </div>
          <div style={{ display: "flex", gap: 8 }}>
            {cameraActive ? (
              <button className="btn btn--ghost" onClick={stopCamera}>
                关闭摄像头
              </button>
            ) : (
              <button className="btn" onClick={startCamera}>
                开启摄像头
              </button>
            )}
          </div>
          {cameraError && (
            <div style={{ color: "#f87171", fontSize: 12, marginTop: 8 }}>
              {cameraError}
            </div>
          )}
        </section>

        <section className="panel">
          <div className="panel__title">录制</div>
          <div style={{ display: "flex", gap: 8 }}>
            {isRecording ? (
              <button className="btn btn--danger" onClick={stopRecording}>
                停止并保存
              </button>
            ) : (
              <button
                className="btn"
                onClick={startRecording}
                disabled={!cameraActive}
              >
                开始录制
              </button>
            )}
            <button className="btn btn--ghost" onClick={refreshRecordings}>
              刷新
            </button>
          </div>
          <div style={{ marginTop: 10 }}>
            {recordings.length === 0 ? (
              <div style={{ fontSize: 12, color: "#6b7280" }}>暂无录制文件</div>
            ) : (
              <div className="prob-list">
                {recordings.map((r) => (
                  <div key={r.filename} className="prob-item">
                    <span className="prob-item__name">{r.filename}</span>
                    <span className="prob-item__value">
                      {formatSize(r.size_bytes)}
                    </span>
                  </div>
                ))}
              </div>
            )}
            {recordings.length > 0 && (
              <button
                className="btn btn--ghost"
                style={{ marginTop: 10, width: "100%" }}
                onClick={openRecordingFolder}
              >
                打开所在目录
              </button>
            )}
          </div>
        </section>

        <section className="panel" style={{ marginTop: "auto" }}>
          <div className="panel__title">提示</div>
          <div style={{ fontSize: 12, color: "#9ca3af", lineHeight: 1.6 }}>
            请将手势置于摄像头画面中央，保持手掌清晰可见。模型推理目标单帧
            &lt; 50ms，前端每 80ms 向后端推送一帧进行推理。
          </div>
        </section>
      </aside>

      <main className="app__main">
        <div className="video-stage">
          <video ref={videoRef} playsInline muted autoPlay />
          <canvas ref={canvasRef} style={{ display: "none" }} />
          <canvas ref={overlayRef} />
          {!cameraActive && (
            <div className="empty-state">
              请先在左侧选择摄像头设备并点击「开启摄像头」
            </div>
          )}
          <div className="overlay">
            {latestResult && canInfer && (
              <>
                <div className="chip chip--label">
                  {displayLabel(latestResult.label)}
                </div>
                <div className="chip chip--confidence">
                  置信度 {(latestResult.confidence * 100).toFixed(1)}% · 推理{" "}
                  {latestResult.inference_ms.toFixed(1)}ms
                </div>
              </>
            )}
          </div>
          <div className="overlay__bottom">
            {stats && (
              <>
                <div className="chip">FPS {stats.fps.toFixed(1)}</div>
                <div className="chip">
                  平均推理 {stats.avg_inference_ms.toFixed(1)}ms
                </div>
              </>
            )}
          </div>
        </div>

        <div className="stats-grid">
          <div className="stat">
            <div className="stat__label">实时 FPS</div>
            <div className="stat__value">
              {stats ? stats.fps.toFixed(1) : "—"}
            </div>
          </div>
          <div className="stat">
            <div className="stat__label">平均推理耗时</div>
            <div className="stat__value">
              {stats ? `${stats.avg_inference_ms.toFixed(1)} ms` : "—"}
            </div>
          </div>
          <div className="stat">
            <div className="stat__label">当前手势</div>
            <div className="stat__value">
              {latestResult ? displayLabel(latestResult.label) : "—"}
            </div>
          </div>
        </div>

        {latestResult && (
          <section className="panel">
            <div className="panel__title">各类别概率</div>
            <div className="prob-list">
              {(modelStatus?.labels ?? []).map((label, i) => {
                const p = latestResult.probabilities[i] ?? 0;
                const isTop = i === latestResult.label_index;
                return (
                  <div
                    key={label}
                    className={`prob-item ${isTop ? "prob-item--top" : ""}`}
                  >
                    <span className="prob-item__name">{displayLabel(label)}</span>
                    <span className="prob-item__value">
                      {(p * 100).toFixed(1)}%
                    </span>
                  </div>
                );
              })}
            </div>
          </section>
        )}

        {error && (
          <div
            style={{
              padding: 10,
              background: "rgba(220,38,38,0.15)",
              border: "1px solid #b91c1c",
              borderRadius: 8,
              color: "#fecaca",
              fontSize: 13,
            }}
          >
            {error}
          </div>
        )}
      </main>
    </div>
  );
}

// ---------- helpers ----------

function drawOverlay(
  canvas: HTMLCanvasElement,
  result: WSResult | null,
  _stats: WSStats | null
): void {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  if (!result) return;

  const w = canvas.width;
  const h = canvas.height;

  // Subtle vignette frame
  ctx.strokeStyle = "rgba(37, 99, 235, 0.7)";
  ctx.lineWidth = Math.max(2, Math.floor(w / 500));
  ctx.strokeRect(
    ctx.lineWidth / 2,
    ctx.lineWidth / 2,
    w - ctx.lineWidth,
    h - ctx.lineWidth
  );

  // Corner brackets
  const size = Math.floor(Math.min(w, h) * 0.12);
  ctx.strokeStyle = "rgba(37, 99, 235, 0.9)";
  ctx.lineWidth = Math.max(3, Math.floor(w / 300));
  drawCorner(ctx, 0, 0, size, size, 1, 1);
  drawCorner(ctx, w, 0, size, size, -1, 1);
  drawCorner(ctx, 0, h, size, size, 1, -1);
  drawCorner(ctx, w, h, size, size, -1, -1);
}

function drawCorner(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  w: number,
  h: number,
  dx: 1 | -1,
  dy: 1 | -1
) {
  ctx.beginPath();
  ctx.moveTo(x, y);
  ctx.lineTo(x + dx * w, y);
  ctx.moveTo(x, y);
  ctx.lineTo(x, y + dy * h);
  ctx.stroke();
}

function arrayBufferToBase64(buffer: ArrayBuffer): string {
  const bytes = new Uint8Array(buffer);
  let binary = "";
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode.apply(
      null,
      Array.from(bytes.subarray(i, i + chunk)) as number[]
    );
  }
  return btoa(binary);
}
