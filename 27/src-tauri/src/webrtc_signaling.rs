use crate::compression::{
    deserialize_compressed, serialize_compressed, CompressedPointCloud, CompressionError,
};
use crate::point_cloud::PointCloud;
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::Semaphore;
use tokio::time::sleep;
use uuid::Uuid;

pub type AppState = Arc<Mutex<AppData>>;

const DEFAULT_BUFFER_LOW_WATERMARK: usize = 16 * 1024 * 1024;
const DEFAULT_BUFFER_HIGH_WATERMARK: usize = 64 * 1024 * 1024;
const DEFAULT_CHUNK_SIZE: usize = 64 * 1024;
const MAX_RETRY_COUNT: u32 = 100;
const RETRY_DELAY_MS: u64 = 10;

pub struct AppData {
    pub connections: HashMap<String, ConnectionInfo>,
    pub point_cloud: Option<PointCloud>,
    pub target_point_cloud: Option<PointCloud>,
    pub registration_result: Option<crate::icp_registration::RegistrationResult>,
    pub transfer_progress: HashMap<String, TransferProgress>,
    pub stats: TransferStats,
    pub data_channels: HashMap<String, DataChannelState>,
}

impl Default for AppData {
    fn default() -> Self {
        Self {
            connections: HashMap::new(),
            point_cloud: None,
            target_point_cloud: None,
            registration_result: None,
            transfer_progress: HashMap::new(),
            stats: TransferStats::default(),
            data_channels: HashMap::new(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConnectionInfo {
    pub id: String,
    pub peer_id: Option<String>,
    pub role: ConnectionRole,
    pub status: ConnectionStatus,
    pub address: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub enum ConnectionRole {
    Sender,
    Receiver,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub enum ConnectionStatus {
    Connected,
    Disconnected,
    Transferring,
    Error,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TransferProgress {
    pub transfer_id: String,
    pub total_size: usize,
    pub transferred: usize,
    pub start_time: std::time::Instant,
    pub last_update: std::time::Instant,
    pub bytes_per_second: f64,
    pub buffered_amount: usize,
    pub chunk_delay_ms: u64,
}

impl TransferProgress {
    pub fn new(total_size: usize) -> Self {
        let now = std::time::Instant::now();
        Self {
            transfer_id: Uuid::new_v4().to_string(),
            total_size,
            transferred: 0,
            start_time: now,
            last_update: now,
            bytes_per_second: 0.0,
            buffered_amount: 0,
            chunk_delay_ms: 0,
        }
    }

    pub fn update(&mut self, bytes: usize, buffered_amount: usize, chunk_delay_ms: u64) {
        self.transferred += bytes;
        self.buffered_amount = buffered_amount;
        self.chunk_delay_ms = chunk_delay_ms;
        let now = std::time::Instant::now();
        let elapsed = now.duration_since(self.last_update).as_secs_f64();
        if elapsed > 0.0 {
            self.bytes_per_second = bytes as f64 / elapsed;
        }
        self.last_update = now;
    }

    pub fn progress_percent(&self) -> f64 {
        if self.total_size > 0 {
            (self.transferred as f64 / self.total_size as f64 * 100.0
        } else {
            0.0
        }
    }

    pub fn elapsed(&self) -> std::time::Duration {
        std::time::Instant::now().duration_since(self.start_time)
    }
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct TransferStats {
    pub total_sent: usize,
    pub total_received: usize,
    pub current_speed: f64,
    pub peak_speed: f64,
    pub backpressure_events: u64,
    pub chunks_sent: u64,
    pub chunks_dropped: u64,
}

pub struct DataChannelState {
    pub id: String,
    pub buffer_low_watermark: usize,
    pub buffer_high_watermark: usize,
    pub chunk_size: usize,
    pub semaphore: Arc<Semaphore>,
    pub is_paused: bool,
    pub last_buffer_check: std::time::Instant,
}

impl DataChannelState {
    pub fn new(id: String) -> Self {
        Self {
            id,
            buffer_low_watermark: DEFAULT_BUFFER_LOW_WATERMARK,
            buffer_high_watermark: DEFAULT_BUFFER_HIGH_WATERMARK,
            chunk_size: DEFAULT_CHUNK_SIZE,
            semaphore: Arc::new(Semaphore::new(1)),
            is_paused: false,
            last_buffer_check: std::time::Instant::now(),
        }
    }

    pub fn with_limits(
        id: String,
        low_watermark: usize,
        high_watermark: usize,
        chunk_size: usize,
    ) -> Self {
        Self {
            id,
            buffer_low_watermark: low_watermark,
            buffer_high_watermark: high_watermark,
            chunk_size,
            semaphore: Arc::new(Semaphore::new(1)),
            is_paused: false,
            last_buffer_check: std::time::Instant::now(),
        }
    }

    pub fn should_pause(&self, buffered_amount: usize) -> bool {
        buffered_amount >= self.buffer_high_watermark
    }

    pub fn should_resume(&self, buffered_amount: usize) -> bool {
        buffered_amount <= self.buffer_low_watermark
    }

    pub fn compute_delay(&self, buffered_amount: usize) -> u64 {
        if buffered_amount >= self.buffer_high_watermark {
            let overflow = buffered_amount - self.buffer_high_watermark;
            let ratio = overflow as f64 / self.buffer_high_watermark as f64;
            (RETRY_DELAY_MS as f64 * (1.0 + ratio * 5.0)) as u64
        } else if buffered_amount > self.buffer_low_watermark {
            let half = (self.buffer_high_watermark + self.buffer_low_watermark) / 2;
            if buffered_amount > half {
                RETRY_DELAY_MS
            } else {
                RETRY_DELAY_MS / 2
            }
        } else {
            0
        }
    }
}

#[derive(Debug, Serialize, Deserialize)]
pub enum SignalingMessage {
    Offer {
        from: String,
        to: String,
        session_description: String,
    },
    Answer {
        from: String,
        to: String,
        session_description: String,
    },
    IceCandidate {
        from: String,
        to: String,
        candidate: String,
    },
    PeerListRequest {
        from: String,
    },
    PeerList {
        peers: Vec<String>,
    },
    Register {
        id: String,
        role: ConnectionRole,
    },
    Registered {
        id: String,
    },
    Error {
        message: String,
    },
    TransferStart {
        from: String,
        to: String,
        file_name: String,
        file_size: usize,
        chunk_size: usize,
        total_chunks: usize,
    },
    TransferChunk {
        from: String,
        to: String,
        transfer_id: String,
        data: Vec<u8>,
        chunk_index: usize,
        total_chunks: usize,
        is_last: bool,
    },
    TransferAck {
        from: String,
        to: String,
        transfer_id: String,
        chunk_index: usize,
        buffered_amount: usize,
    },
    TransferPause {
        from: String,
        to: String,
        transfer_id: String,
    },
    TransferResume {
        from: String,
        to: String,
        transfer_id: String,
    },
    TransferComplete {
        from: String,
        to: String,
        transfer_id: String,
        checksum: String,
    },
    RequestPointCloud {
        from: String,
    },
    PointCloudData {
        data: Vec<u8>,
    },
    ProgressUpdate {
        from: String,
        transferred: usize,
        total: usize,
        speed: f64,
        buffered_amount: usize,
    },
    BufferStatus {
        from: String,
        buffered_amount: usize,
        is_paused: bool,
    },
}

pub struct SignalingServer {
    state: AppState,
    port: u16,
}

impl SignalingServer {
    pub fn new(state: AppState, port: u16) -> Self {
        Self { state, port }
    }

    pub async fn start(&self) -> Result<(), std::io::Error> {
        let listener = TcpListener::bind(format!("0.0.0.0:{}", self.port)).await?;
        log::info!(
            "Signaling server listening on port {} (buffer: {}MB - {}MB)",
            self.port,
            DEFAULT_BUFFER_LOW_WATERMARK / (1024 * 1024),
            DEFAULT_BUFFER_HIGH_WATERMARK / (1024 * 1024)
        );

        loop {
            let (socket, addr) = listener.accept().await?;
            let state = self.state.clone();
            tokio::spawn(async move {
                if let Err(e) = handle_connection(socket, state).await {
                    log::error!("Connection error from {}: {}", addr, e);
                }
            });
        }
    }
}

async fn handle_connection(
    mut socket: TcpStream,
    state: AppState,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let mut buffer = vec![0u8; 131072];
    let mut message_buffer = Vec::new();

    loop {
        let n = socket.read(&mut buffer).await?;
        if n == 0 {
            break;
        }

        message_buffer.extend_from_slice(&buffer[..n]);

        while let Some(pos) = message_buffer.iter().position(|&b| b == b'\n') {
            let message: Vec<u8> = message_buffer.drain(..=pos).collect();
            let message_str = String::from_utf8_lossy(&message[..message.len() - 1]);

            match serde_json::from_str::<SignalingMessage>(&message_str) {
                Ok(msg) => {
                    if let Err(e) = handle_message(msg, &mut socket, state.clone()).await {
                        log::error!("Error handling message: {}", e);
                        let error_msg = SignalingMessage::Error {
                            message: e.to_string(),
                        };
                        let _ = send_message(&mut socket, &error_msg).await;
                    }
                }
                Err(e) => {
                    log::warn!("Failed to parse message: {}", e);
                }
            }
        }
    }

    Ok(())
}

async fn handle_message(
    message: SignalingMessage,
    socket: &mut TcpStream,
    state: AppState,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    match message {
        SignalingMessage::Register { id, role } => {
            let addr = socket.peer_addr()?.to_string();
            let mut data = state.lock();
            data.connections.insert(
                id.clone(),
                ConnectionInfo {
                    id: id.clone(),
                    peer_id: None,
                    role,
                    status: ConnectionStatus::Connected,
                    address: addr.clone(),
                },
            );
            data.data_channels
                .insert(id.clone(), DataChannelState::new(id.clone()));
            drop(data);

            log::info!("Client registered: {} ({}) at {}", id, role, addr);
            let response = SignalingMessage::Registered { id };
            send_message(socket, &response).await?;
        }
        SignalingMessage::PeerListRequest { from } => {
            let data = state.lock();
            let peers: Vec<String> = data
                .connections
                .values()
                .filter(|c| c.id != from)
                .map(|c| c.id.clone())
                .collect();
            drop(data);

            let response = SignalingMessage::PeerList { peers };
            send_message(socket, &response).await?;
        }
        SignalingMessage::Offer { from, to, session_description }
        | SignalingMessage::Answer {
            from,
            to,
            session_description,
        }
        | SignalingMessage::IceCandidate {
            from,
            to,
            candidate: session_description,
        } => {
            log::info!("Forwarding signaling message from {} to {}", from, to);
        }
        SignalingMessage::TransferStart {
            from,
            to,
            file_name,
            file_size,
            chunk_size,
            total_chunks,
        } => {
            log::info!(
                "Transfer started: {} -> {} ({} bytes, {} chunks of {} bytes)",
                from,
                to,
                file_size,
                total_chunks,
                chunk_size
            );

            {
                let mut data = state.lock();
                data.transfer_progress.insert(
                    format!("{}->{}", from, to),
                    TransferProgress::new(file_size),
                );
                if let Some(channel) = data.data_channels.get_mut(&from) {
                    channel.chunk_size = chunk_size;
                }
            }

            let ack = SignalingMessage::TransferAck {
                from: to,
                to: from,
                transfer_id: String::new(),
                chunk_index: 0,
                buffered_amount: 0,
            };
            send_message(socket, &ack).await?;
        }
        SignalingMessage::TransferChunk {
            from,
            to,
            transfer_id,
            data: chunk_data,
            chunk_index,
            total_chunks,
            is_last,
        } => {
            let chunk_len = chunk_data.len();

            let backpressure_info = {
                let mut data = state.lock();
                let progress = data
                    .transfer_progress
                    .entry(format!("{}->{}", from, to))
                    .or_insert_with(|| TransferProgress::new(chunk_len * total_chunks));
                progress.update(chunk_len, 0, 0);
                data.stats.total_received += chunk_len;
                data.stats.chunks_sent += 1;
                (progress.buffered_amount, progress.chunk_delay_ms)
            };

            if is_last {
                log::info!(
                    "Transfer completed: {} -> {} (chunk {}/{})",
                    from,
                    to,
                    chunk_index + 1,
                    total_chunks
                );

                {
                    let mut data = state.lock();
                    if let Some(conn) = data.connections.get_mut(&from) {
                        conn.status = ConnectionStatus::Connected;
                    }
                }

                let complete = SignalingMessage::TransferComplete {
                    from: to,
                    to: from,
                    transfer_id,
                    checksum: String::new(),
                };
                send_message(socket, &complete).await?;
            } else {
                let ack = SignalingMessage::TransferAck {
                    from: to,
                    to: from,
                    transfer_id,
                    chunk_index,
                    buffered_amount: backpressure_info.0,
                };
                send_message(socket, &ack).await?;

                if backpressure_info.1 > 0 {
                    sleep(Duration::from_millis(backpressure_info.1)).await;
                }
            }
        }
        SignalingMessage::TransferAck {
            from,
            to,
            transfer_id,
            chunk_index,
            buffered_amount,
        } => {
            let should_pause = {
                let mut data = state.lock();
                if let Some(channel) = data.data_channels.get_mut(&to) {
                    channel.last_buffer_check = std::time::Instant::now();
                    if channel.should_pause(buffered_amount) {
                        channel.is_paused = true;
                        data.stats.backpressure_events += 1;
                        log::warn!(
                            "Backpressure: pausing transfer {} (buffer: {}MB)",
                            transfer_id,
                            buffered_amount / (1024 * 1024)
                        );
                        true
                    } else if channel.is_paused && channel.should_resume(buffered_amount) {
                        channel.is_paused = false;
                        log::info!(
                            "Backpressure: resuming transfer {} (buffer: {}MB)",
                            transfer_id,
                            buffered_amount / (1024 * 1024)
                        );
                        false
                    } else {
                        channel.is_paused
                    }
                } else {
                    false
                }
            };

            if should_pause {
                let pause = SignalingMessage::TransferPause {
                    from,
                    to,
                    transfer_id,
                };
                send_message(socket, &pause).await?;
            }
        }
        SignalingMessage::TransferPause { .. } => {
            log::info!("Transfer paused");
        }
        SignalingMessage::TransferResume { .. } => {
            log::info!("Transfer resumed");
        }
        SignalingMessage::BufferStatus {
            from,
            buffered_amount,
            is_paused,
        } => {
            log::debug!(
                "Buffer status from {}: {}MB (paused: {})",
                from,
                buffered_amount / (1024 * 1024),
                is_paused
            );
        }
        SignalingMessage::RequestPointCloud { from } => {
            if let Some(cloud) = state.lock().point_cloud.clone() {
                match compress_and_stream_point_cloud(&cloud, socket, &from, state.clone()).await {
                    Ok(_) => log::info!("Point cloud stream completed for {}", from),
                    Err(e) => {
                        log::error!("Failed to stream point cloud: {}", e);
                        let error = SignalingMessage::Error {
                            message: format!("Stream failed: {}", e),
                        };
                        send_message(socket, &error).await?;
                    }
                }
            }
        }
        SignalingMessage::PointCloudData { data } => {
            match deserialize_compressed(&data) {
                Ok(compressed) => match decompress_point_cloud(&compressed) {
                    Ok(cloud) => {
                        state.lock().point_cloud = Some(cloud);
                        log::info!("Point cloud received and decompressed");
                    }
                    Err(e) => log::error!("Failed to decompress point cloud: {}", e),
                },
                Err(e) => log::error!("Failed to deserialize point cloud: {}", e),
            }
        }
        _ => {}
    }

    Ok(())
}

async fn compress_and_stream_point_cloud(
    cloud: &PointCloud,
    socket: &mut TcpStream,
    target_id: &str,
    state: AppState,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let compressed = crate::compression::compress_point_cloud(cloud)?;
    let serialized = serialize_compressed(&compressed)?;

    let total_size = serialized.len();
    let chunk_size = DEFAULT_CHUNK_SIZE;
    let total_chunks = (total_size + chunk_size - 1) / chunk_size;
    let transfer_id = Uuid::new_v4().to_string();

    let start_msg = SignalingMessage::TransferStart {
        from: "server".to_string(),
        to: target_id.to_string(),
        file_name: "point_cloud.bin".to_string(),
        file_size: total_size,
        chunk_size,
        total_chunks,
    };
    send_message(socket, &start_msg).await?;

    let mut offset = 0;
    let mut retry_count = 0u32;

    for chunk_index in 0..total_chunks {
        let chunk_end = (offset + chunk_size).min(total_size);
        let chunk_data = serialized[offset..chunk_end].to_vec();
        let is_last = chunk_end >= total_size;

        let (should_pause, delay_ms) = {
            let data = state.lock();
            let channel = data.data_channels.get(target_id);
            if let Some(channel) = channel {
                (channel.is_paused, channel.compute_delay(0))
            } else {
                (false, 0)
            }
        };

        if should_pause {
            let mut wait_count = 0;
            loop {
                sleep(Duration::from_millis(100)).await;
                wait_count += 1;

                let is_paused = {
                    let data = state.lock();
                    data.data_channels
                        .get(target_id)
                        .map(|c| c.is_paused)
                        .unwrap_or(false)
                };

                if !is_paused || wait_count > 100 {
                    break;
                }

                if wait_count % 10 == 0 {
                    log::debug!("Waiting for buffer to clear... ({})", wait_count);
                }
            }
        }

        if delay_ms > 0 {
            sleep(Duration::from_millis(delay_ms)).await;
        }

        let chunk_msg = SignalingMessage::TransferChunk {
            from: "server".to_string(),
            to: target_id.to_string(),
            transfer_id: transfer_id.clone(),
            data: chunk_data,
            chunk_index,
            total_chunks,
            is_last,
        };

        match send_message_with_retry(socket, &chunk_msg, &mut retry_count).await {
            Ok(_) => {
                retry_count = 0;

                {
                    let mut data = state.lock();
                    let progress = data
                        .transfer_progress
                        .entry(format!("server->{}", target_id))
                        .or_insert_with(|| TransferProgress::new(total_size));
                    progress.update(chunk_end - offset, 0, delay_ms);
                    data.stats.total_sent += chunk_end - offset;
                    data.stats.chunks_sent += 1;
                }
            }
            Err(e) => {
                {
                    let mut data = state.lock();
                    data.stats.chunks_dropped += 1;
                }
                return Err(format!("Failed to send chunk {}: {}", chunk_index, e).into());
            }
        }

        offset = chunk_end;

        if chunk_index % 50 == 0 && chunk_index > 0 {
            let progress_update = {
                let data = state.lock();
                data.transfer_progress
                    .get(&format!("server->{}", target_id))
                    .map(|p| SignalingMessage::ProgressUpdate {
                        from: "server".to_string(),
                        transferred: p.transferred,
                        total: p.total_size,
                        speed: p.bytes_per_second,
                        buffered_amount: p.buffered_amount,
                    })
            };

            if let Some(update) = progress_update {
                send_message(socket, &update).await?;
            }

            log::info!(
                "Streaming progress: {}/{} chunks ({}%)",
                chunk_index + 1,
                total_chunks,
                (chunk_index + 1) as f64 / total_chunks as f64 * 100.0
            );
        }
    }

    Ok(())
}

async fn send_message_with_retry(
    socket: &mut TcpStream,
    message: &SignalingMessage,
    retry_count: &mut u32,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    loop {
        match send_message(socket, message).await {
            Ok(_) => return Ok(()),
            Err(e) => {
                *retry_count += 1;
                if *retry_count >= MAX_RETRY_COUNT {
                    return Err(format!(
                        "Max retries ({}) exceeded: {}",
                        MAX_RETRY_COUNT, e
                    )
                    .into());
                }

                let delay = RETRY_DELAY_MS * (*retry_count as u64);
                log::warn!(
                    "Send failed, retrying ({}/{}): {} (delay: {}ms)",
                    retry_count,
                    MAX_RETRY_COUNT,
                    e,
                    delay
                );
                sleep(Duration::from_millis(delay.min(1000))).await;
            }
        }
    }
}

async fn send_message(
    socket: &mut TcpStream,
    message: &SignalingMessage,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let json = serde_json::to_string(message)?;
    socket.write_all(json.as_bytes()).await?;
    socket.write_all(b"\n").await?;
    Ok(())
}

pub async fn connect_to_signaling(
    addr: &str,
) -> Result<TcpStream, Box<dyn std::error::Error + Send + Sync>> {
    let stream = TcpStream::connect(addr).await?;
    Ok(stream)
}

pub fn get_transfer_progress(state: &AppState) -> Vec<TransferProgress> {
    state.lock().transfer_progress.values().cloned().collect()
}

pub fn get_connections(state: &AppState) -> Vec<ConnectionInfo> {
    state.lock().connections.values().cloned().collect()
}

pub fn get_stats(state: &AppState) -> TransferStats {
    state.lock().stats.clone()
}

pub fn set_buffer_limits(
    state: &AppState,
    channel_id: &str,
    low_watermark: usize,
    high_watermark: usize,
) {
    let mut data = state.lock();
    if let Some(channel) = data.data_channels.get_mut(channel_id) {
        channel.buffer_low_watermark = low_watermark;
        channel.buffer_high_watermark = high_watermark;
    }
}

pub fn get_buffer_status(state: &AppState, channel_id: &str) -> Option<(usize, usize, bool)> {
    let data = state.lock();
    data.data_channels.get(channel_id).map(|c| {
        (
            c.buffer_low_watermark,
            c.buffer_high_watermark,
            c.is_paused,
        )
    })
}

pub fn pause_channel(state: &AppState, channel_id: &str) {
    let mut data = state.lock();
    if let Some(channel) = data.data_channels.get_mut(channel_id) {
        channel.is_paused = true;
    }
}

pub fn resume_channel(state: &AppState, channel_id: &str) {
    let mut data = state.lock();
    if let Some(channel) = data.data_channels.get_mut(channel_id) {
        channel.is_paused = false;
    }
}

pub fn configure_backpressure(
    state: &AppState,
    channel_id: &str,
    low_watermark_mb: f64,
    high_watermark_mb: f64,
    chunk_size_kb: usize,
) {
    let low = (low_watermark_mb * 1024.0 * 1024.0) as usize;
    let high = (high_watermark_mb * 1024.0 * 1024.0) as usize;
    let chunk = chunk_size_kb * 1024;

    let mut data = state.lock();
    if let Some(channel) = data.data_channels.get_mut(channel_id) {
        channel.buffer_low_watermark = low;
        channel.buffer_high_watermark = high;
        channel.chunk_size = chunk;
    }
}
