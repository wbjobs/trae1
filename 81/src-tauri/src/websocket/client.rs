use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use futures_util::{SinkExt, StreamExt};
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;
use tokio_tungstenite::{connect_async, tungstenite::Message};

use crate::inference::CommandResult;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SmartHomeMessage {
    pub action: String,
    pub label: String,
    pub confidence: f32,
    pub device: String,
    pub timestamp: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SmartHomeResponse {
    pub success: bool,
    pub action: String,
    pub message: String,
    pub timestamp: u64,
}

#[derive(Clone)]
pub struct SmartHomeClient {
    url: String,
    connected: Arc<AtomicBool>,
    sender: Arc<Mutex<Option<mpsc::Sender<SmartHomeMessage>>>>,
    reconnect_task: Arc<Mutex<Option<tokio::task::JoinHandle<()>>>>,
}

impl SmartHomeClient {
    pub fn new(url: String) -> Self {
        Self {
            url,
            connected: Arc::new(AtomicBool::new(false)),
            sender: Arc::new(Mutex::new(None)),
            reconnect_task: Arc::new(Mutex::new(None)),
        }
    }

    pub fn is_connected(&self) -> bool {
        self.connected.load(Ordering::SeqCst)
    }

    pub fn connect(&self) {
        if self.connected.load(Ordering::SeqCst) {
            return;
        }

        let url = self.url.clone();
        let connected = Arc::clone(&self.connected);
        let sender_arc = Arc::clone(&self.sender);

        let handle = tokio::spawn(async move {
            loop {
                log::info!("正在连接智能家居服务: {}", url);

                match connect_async(&url).await {
                    Ok((ws_stream, _)) => {
                        log::info!("已连接到智能家居服务");
                        connected.store(true, Ordering::SeqCst);

                        let (mut write, mut read) = ws_stream.split();
                        let (tx, mut rx) = mpsc::channel::<SmartHomeMessage>(100);
                        *sender_arc.lock() = Some(tx);

                        let send_task = tokio::spawn(async move {
                            while let Some(msg) = rx.recv().await {
                                let json = match serde_json::to_string(&msg) {
                                    Ok(j) => j,
                                    Err(e) => {
                                        log::error!("序列化消息失败: {}", e);
                                        continue;
                                    }
                                };
                                if let Err(e) = write.send(Message::Text(json.into())).await {
                                    log::error!("发送消息失败: {}", e);
                                    break;
                                }
                            }
                        });

                        let recv_task = tokio::spawn(async move {
                            while let Some(msg) = read.next().await {
                                match msg {
                                    Ok(Message::Text(text)) => {
                                        if let Ok(resp) =
                                            serde_json::from_str::<SmartHomeResponse>(&text)
                                        {
                                            if resp.success {
                                                log::info!(
                                                    "智能家居响应 - 动作: {}, 消息: {}",
                                                    resp.action,
                                                    resp.message
                                                );
                                            } else {
                                                log::warn!(
                                                    "智能家居执行失败 - 动作: {}, 消息: {}",
                                                    resp.action,
                                                    resp.message
                                                );
                                            }
                                        }
                                    }
                                    Ok(Message::Close(_)) => break,
                                    Err(e) => {
                                        log::error!("WebSocket接收错误: {}", e);
                                        break;
                                    }
                                    _ => {}
                                }
                            }
                        });

                        let _ = tokio::join!(send_task, recv_task);
                    }
                    Err(e) => {
                        log::warn!("连接智能家居服务失败: {}", e);
                    }
                }

                connected.store(false, Ordering::SeqCst);
                *sender_arc.lock() = None;

                log::info!("3秒后尝试重连...");
                tokio::time::sleep(Duration::from_secs(3)).await;
            }
        });

        *self.reconnect_task.lock() = Some(handle);
    }

    pub fn send_command(&self, result: &CommandResult) -> bool {
        if !self.connected.load(Ordering::SeqCst) {
            log::warn!("未连接到智能家居服务，跳过发送");
            return false;
        }

        let action = crate::inference::CommandLabel::from_index(result.label_index)
            .to_smart_home_action()
            .to_string();

        let msg = SmartHomeMessage {
            action,
            label: result.label.clone(),
            confidence: result.confidence,
            device: "voice-assistant".to_string(),
            timestamp: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs(),
        };

        if let Some(ref sender) = *self.sender.lock() {
            match sender.try_send(msg) {
                Ok(_) => true,
                Err(e) => {
                    log::error!("发送命令到智能家居失败: {}", e);
                    false
                }
            }
        } else {
            false
        }
    }

    pub fn send_custom_command(&self, label: &str, action: &str, confidence: f32) -> bool {
        if !self.connected.load(Ordering::SeqCst) {
            log::warn!("未连接到智能家居服务，跳过发送自定义命令");
            return false;
        }

        let msg = SmartHomeMessage {
            action: action.to_string(),
            label: label.to_string(),
            confidence,
            device: "voice-assistant-custom".to_string(),
            timestamp: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs(),
        };

        if let Some(ref sender) = *self.sender.lock() {
            match sender.try_send(msg) {
                Ok(_) => {
                    log::info!(
                        "自定义命令已发送: {} -> {}",
                        label,
                        action
                    );
                    true
                }
                Err(e) => {
                    log::error!("发送自定义命令到智能家居失败: {}", e);
                    false
                }
            }
        } else {
            false
        }
    }

    pub fn disconnect(&self) {
        if let Some(handle) = self.reconnect_task.lock().take() {
            handle.abort();
        }
        self.connected.store(false, Ordering::SeqCst);
        *self.sender.lock() = None;
        log::info!("WebSocket连接已断开");
    }
}
