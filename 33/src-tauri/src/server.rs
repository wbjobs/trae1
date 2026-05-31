use std::collections::VecDeque;
use std::sync::atomic::{AtomicU16, Ordering};
use std::sync::Arc;
use std::time::Instant;

use futures_util::{SinkExt, StreamExt};
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::mpsc;
use tokio_tungstenite::tungstenite::Message;
use tauri::{AppHandle, Manager};

use crate::error::AppError;
use crate::inference::{InferenceResult, GesturePosition};
use crate::state::AppState;

const PORT: AtomicU16 = AtomicU16::new(0);
const TEMPORAL_WINDOW_SIZE: usize = 5;
const TEMPORAL_MIN_FRAMES: usize = 3;

#[derive(Debug, Clone, Serialize)]
pub struct Position {
    pub x: f32,
    pub y: f32,
    pub confidence: f32,
}

#[tauri::command]
pub async fn start_ws_server() -> Result<u16, AppError> {
    Ok(PORT.load(Ordering::SeqCst))
}

#[tauri::command]
pub async fn get_ws_port() -> Result<u16, AppError> {
    Ok(PORT.load(Ordering::SeqCst))
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum ServerMessage {
    #[serde(rename = "frame")]
    Frame { data: String, timestamp: f64 },
    #[serde(rename = "ping")]
    Ping { timestamp: f64 },
}

#[derive(Debug, Serialize)]
#[serde(tag = "type")]
pub enum ClientMessage {
    #[serde(rename = "result")]
    Result {
        label: String,
        label_index: usize,
        confidence: f32,
        probabilities: Vec<f32>,
        inference_ms: f32,
        total_ms: f32,
        timestamp: f64,
        position: Option<Position>,
    },
    #[serde(rename = "error")]
    Error { message: String },
    #[serde(rename = "stats")]
    Stats {
        fps: f32,
        avg_inference_ms: f32,
        queue_depth: usize,
    },
    #[serde(rename = "status")]
    Status {
        model_loaded: bool,
        port: u16,
    },
}

pub async fn run_server(handle: &AppHandle, state: AppState) -> Result<(), AppError> {
    let listener = TcpListener::bind("127.0.0.1:0").await?;
    let addr = listener.local_addr()?;
    PORT.store(addr.port(), Ordering::SeqCst);
    tracing::info!("WebSocket inference server listening on {addr}");

    let port = addr.port();
    handle.emit("ws-ready", port)?;

    loop {
        match listener.accept().await {
            Ok((stream, remote)) => {
                tracing::info!("Accepted WebSocket connection from {remote}");
                let state = state.clone();
                tokio::spawn(async move {
                    if let Err(e) = handle_connection(stream, state).await {
                        tracing::warn!("Client disconnected: {e}");
                    }
                });
            }
            Err(e) => {
                tracing::error!("Failed to accept connection: {e}");
            }
        }
    }
}

async fn handle_connection(stream: TcpStream, state: AppState) -> Result<(), AppError> {
    let addr = stream.peer_addr().ok();
    let ws_stream = tokio_tungstenite::accept_async(stream)
        .await
        .map_err(|e| AppError::Other(format!("WebSocket handshake failed: {e}")))?;

    let (mut write, mut read) = ws_stream.split();

    let (tx, mut rx) = mpsc::channel::<Message>(64);
    tokio::spawn(async move {
        while let Some(msg) = rx.recv().await {
            if write.send(msg).await.is_err() {
                break;
            }
        }
    });

    let stats = Arc::new(Mutex::new(StatsAccumulator::new()));
    let temporal = Arc::new(Mutex::new(TemporalSmoother::new(
        TEMPORAL_WINDOW_SIZE,
        TEMPORAL_MIN_FRAMES,
    )));

    let initial_status = serde_json::to_vec(&ClientMessage::Status {
        model_loaded: state.engine.lock().is_some(),
        port: PORT.load(Ordering::SeqCst),
    })
    .unwrap_or_default();
    let _ = tx.send(Message::binary(initial_status)).await;

    while let Some(msg) = read.next().await {
        let msg = msg.map_err(|e| AppError::Other(format!("WS read error: {e}")))?;
        match msg {
            Message::Text(text) => {
                handle_text_message(&text, &state, &tx, &stats, &temporal).await;
            }
            Message::Binary(data) => {
                handle_binary_message(data, &state, &tx, &stats, &temporal).await;
            }
            Message::Ping(_) | Message::Pong(_) => {}
            Message::Close(_) => break,
            Message::Frame(_) => {}
        }
    }

    tracing::info!("Client {addr:?} disconnected");
    Ok(())
}

async fn handle_text_message(
    text: &str,
    state: &AppState,
    tx: &mpsc::Sender<Message>,
    stats: &Arc<Mutex<StatsAccumulator>>,
    temporal: &Arc<Mutex<TemporalSmoother>>,
) {
    let Ok(msg) = serde_json::from_str::<ServerMessage>(text) else {
        send_error(tx, "Invalid JSON message").await;
        return;
    };

    match msg {
        ServerMessage::Frame { data, timestamp } => {
            let Ok(bytes) = base64_decode(&data) else {
                send_error(tx, "Invalid base64 frame").await;
                return;
            };
            run_inference(bytes, timestamp, state, tx, stats, temporal).await;
        }
        ServerMessage::Ping { timestamp } => {
            let stats_snapshot = stats.lock().snapshot();
            let msg = ClientMessage::Stats {
                fps: stats_snapshot.0,
                avg_inference_ms: stats_snapshot.1,
                queue_depth: 0,
            };
            if let Ok(payload) = serde_json::to_vec(&msg) {
                let _ = tx.send(Message::binary(payload)).await;
            }
            let _ = timestamp;
        }
    }
}

async fn handle_binary_message(
    data: Vec<u8>,
    state: &AppState,
    tx: &mpsc::Sender<Message>,
    stats: &Arc<Mutex<StatsAccumulator>>,
    temporal: &Arc<Mutex<TemporalSmoother>>,
) {
    run_inference(data, now_secs(), state, tx, stats, temporal).await;
}

async fn run_inference(
    bytes: Vec<u8>,
    timestamp: f64,
    state: &AppState,
    tx: &mpsc::Sender<Message>,
    stats: &Arc<Mutex<StatsAccumulator>>,
    temporal: &Arc<Mutex<TemporalSmoother>>,
) {
    let total_start = Instant::now();

    let engine_guard = state.engine.lock();
    let Some(engine) = engine_guard.as_ref() else {
        send_error(tx, "Model not loaded").await;
        return;
    };

    match engine.infer_from_bytes(&bytes) {
        Ok(result) => {
            let total_ms = total_start.elapsed().as_secs_f32() * 1000.0;

            {
                let mut s = stats.lock();
                s.push(result.inference_ms);
            }
            let stats_snapshot = stats.lock().snapshot();

            // Apply temporal smoothing: majority voting + probability averaging
            let smoothed = {
                let mut t = temporal.lock();
                t.push(&result);
                t.smoothed(&engine.labels)
            };
            let smoothed_label = smoothed.label(&engine.labels);

            let position = result.position.as_ref().map(|p| Position {
                x: p.x,
                y: p.y,
                confidence: p.region_confidence,
            });

            let msg = ClientMessage::Result {
                label: smoothed_label,
                label_index: smoothed.label_index,
                confidence: smoothed.confidence,
                probabilities: smoothed.probabilities,
                inference_ms: result.inference_ms,
                total_ms,
                timestamp,
                position,
            };

            if let Ok(payload) = serde_json::to_vec(&msg) {
                let _ = tx.send(Message::binary(payload)).await;
            }

            let stats_msg = ClientMessage::Stats {
                fps: stats_snapshot.0,
                avg_inference_ms: stats_snapshot.1,
                queue_depth: 0,
            };
            if let Ok(payload) = serde_json::to_vec(&stats_msg) {
                let _ = tx.send(Message::binary(payload)).await;
            }
        }
        Err(e) => {
            send_error(tx, &e.to_string()).await;
        }
    }
}

async fn send_error(tx: &mpsc::Sender<Message>, message: &str) {
    let msg = ClientMessage::Error {
        message: message.to_string(),
    };
    if let Ok(payload) = serde_json::to_vec(&msg) {
        let _ = tx.send(Message::binary(payload)).await;
    }
}

fn base64_decode(s: &str) -> Result<Vec<u8>, AppError> {
    let cleaned: String = s.chars().filter(|c| !c.is_whitespace()).collect();
    if cleaned.is_empty() {
        return Ok(Vec::new());
    }

    const ALPHABET: &[u8; 64] =
        b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    let bytes = cleaned.as_bytes();
    let mut out = Vec::with_capacity((bytes.len() / 4) * 3);
    let mut i = 0;
    while i + 4 <= bytes.len() {
        let c = |b: u8| -> u32 {
            if b == b'=' {
                0
            } else {
                ALPHABET.iter().position(|&x| x == b).map(|x| x as u32).unwrap_or(0)
            }
        };
        let a = c(bytes[i]);
        let b = c(bytes[i + 1]);
        let c1 = c(bytes[i + 2]);
        let d = c(bytes[i + 3]);
        let triple = (a << 18) | (b << 12) | (c1 << 6) | d;
        out.push(((triple >> 16) & 0xFF) as u8);
        if bytes[i + 2] != b'=' {
            out.push(((triple >> 8) & 0xFF) as u8);
        }
        if bytes[i + 3] != b'=' {
            out.push((triple & 0xFF) as u8);
        }
        i += 4;
    }
    Ok(out)
}

fn now_secs() -> f64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs_f64())
        .unwrap_or(0.0)
}

struct StatsAccumulator {
    times: VecDeque<f32>,
    start: Instant,
    count: u32,
}

impl StatsAccumulator {
    fn new() -> Self {
        Self {
            times: VecDeque::with_capacity(120),
            start: Instant::now(),
            count: 0,
        }
    }

    fn push(&mut self, inference_ms: f32) {
        self.times.push_back(inference_ms);
        if self.times.len() > 60 {
            self.times.pop_front();
        }
        self.count += 1;
    }

    fn snapshot(&self) -> (f32, f32) {
        let elapsed = self.start.elapsed().as_secs_f32();
        let fps = if elapsed > 0.0 {
            self.count as f32 / elapsed
        } else {
            0.0
        };
        let avg = if self.times.is_empty() {
            0.0
        } else {
            self.times.iter().sum::<f32>() / self.times.len() as f32
        };
        (fps, avg)
    }
}

// =============================================================================
// Temporal smoothing via majority voting over a sliding window of frames
// =============================================================================

#[derive(Clone)]
struct SmoothedFrame {
    label_index: usize,
    confidence: f32,
    probabilities: Vec<f32>,
}

pub struct TemporalSmoother {
    window_size: usize,
    min_frames: usize,
    history: VecDeque<SmoothedFrame>,
}

impl TemporalSmoother {
    fn new(window_size: usize, min_frames: usize) -> Self {
        Self {
            window_size,
            min_frames,
            history: VecDeque::with_capacity(window_size),
        }
    }

    fn push(&mut self, result: &InferenceResult) {
        self.history.push_back(SmoothedFrame {
            label_index: result.label_index,
            confidence: result.confidence,
            probabilities: result.probabilities.clone(),
        });
        if self.history.len() > self.window_size {
            self.history.pop_front();
        }
    }

    fn smoothed(&self, labels: &[String]) -> SmoothedFrame {
        if self.history.len() < self.min_frames {
            let latest = self.history.back().cloned().unwrap_or(SmoothedFrame {
                label_index: 0,
                confidence: 0.0,
                probabilities: Vec::new(),
            });
            return latest;
        }

        // Majority vote for label index
        let num_classes = labels.len();
        let mut votes = vec![0usize; num_classes];
        for frame in &self.history {
            if frame.label_index < num_classes {
                votes[frame.label_index] += 1;
            }
        }
        let (label_index, _) = votes
            .iter()
            .enumerate()
            .max_by_key(|(_, v)| *v)
            .unwrap_or((0, &0));

        // Average probabilities across window (only for winning label's class)
        let avg_probs = if self.history.is_empty() {
            Vec::new()
        } else {
            let n = self.history.len();
            let n_classes = self.history[0].probabilities.len();
            let mut avg = vec![0.0f32; n_classes];
            for frame in &self.history {
                for (i, p) in frame.probabilities.iter().enumerate() {
                    if i < n_classes {
                        avg[i] += p;
                    }
                }
            }
            for p in &mut avg {
                *p /= n as f32;
            }
            avg
        };

        let smoothed_confidence = avg_probs.get(label_index).copied().unwrap_or(0.0);

        SmoothedFrame {
            label_index,
            confidence: smoothed_confidence,
            probabilities: avg_probs,
        }
    }
}

// Re-export for external use
impl SmoothedFrame {
    pub fn label(&self, labels: &[String]) -> String {
        labels
            .get(self.label_index)
            .cloned()
            .unwrap_or_else(|| format!("class_{}", self.label_index))
    }
}
