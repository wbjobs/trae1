use std::net::SocketAddr;
use std::sync::Arc;
use std::time::Duration;

use anyhow::{Context, Result};
use futures::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use tokio::net::UdpSocket;
use tokio::sync::mpsc;
use tokio_tungstenite::{connect_async, tungstenite::Message};

use crate::cli::CongestionControl;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum SignalingMessage {
    Register { peer_id: String },
    PeerQuery { peer_id: String },
    PeerInfo { peer_id: String, addr: String },
    PeerNotFound { peer_id: String },
    ConnectRequest { from: String, to: String },
    ConnectResponse { from: String, addr: String },
    HolePunch { from: String, to: String, addr: String },
    RelayData { from: String, to: String, data: Vec<u8> },
    Error { message: String },
    PeerList { peers: Vec<String> },
    PeerListRequest,
}

#[derive(Debug, Clone)]
pub struct PeerInfo {
    pub peer_id: String,
    pub public_addr: String,
}

pub struct P2PClient {
    signaling_url: String,
    peer_id: String,
    signaling_tx: mpsc::Sender<SignalingMessage>,
    signaling_rx: mpsc::Receiver<SignalingMessage>,
}

impl P2PClient {
    pub async fn connect(signaling_url: &str, peer_id: &str) -> Result<Self> {
        let (ws_stream, _) = connect_async(signaling_url.to_string()).await
            .context("Failed to connect to signaling server")?;
        let (mut ws_write, mut ws_read) = ws_stream.split();

        let (tx_out, mut rx_out) = mpsc::channel::<SignalingMessage>(32);
        let (tx_in, rx_in) = mpsc::channel::<SignalingMessage>(32);

        tokio::spawn(async move {
            loop {
                tokio::select! {
                    msg = rx_out.recv() => {
                        match msg {
                            Some(msg) => {
                                let json = serde_json::to_string(&msg).unwrap();
                                if ws_write.send(Message::Text(json.into())).await.is_err() {
                                    break;
                                }
                            }
                            None => break,
                        }
                    }
                    msg = ws_read.next() => {
                        match msg {
                            Some(Ok(Message::Text(text))) => {
                                if let Ok(sig_msg) = serde_json::from_str::<SignalingMessage>(&text) {
                                    let _ = tx_in.send(sig_msg).await;
                                }
                            }
                            Some(Ok(Message::Close(_))) | None => break,
                            _ => {}
                        }
                    }
                }
            }
        });

        let client = Self {
            signaling_url: signaling_url.to_string(),
            peer_id: peer_id.to_string(),
            signaling_tx: tx_out,
            signaling_rx: rx_in,
        };

        client.register().await?;

        Ok(client)
    }

    async fn register(&self) -> Result<()> {
        let msg = SignalingMessage::Register {
            peer_id: self.peer_id.clone(),
        };
        self.signaling_tx.send(msg).await
            .map_err(|_| anyhow::anyhow!("Failed to send register message"))?;

        Ok(())
    }

    pub async fn query_peer(&mut self, peer_id: &str) -> Result<Option<PeerInfo>> {
        let msg = SignalingMessage::PeerQuery {
            peer_id: peer_id.to_string(),
        };
        self.signaling_tx.send(msg).await
            .map_err(|_| anyhow::anyhow!("Failed to send peer query"))?;

        let response = tokio::time::timeout(
            Duration::from_secs(5),
            self.signaling_rx.recv(),
        )
        .await
        .context("Timeout waiting for peer info")?
        .context("Signaling channel closed")?;

        match response {
            SignalingMessage::PeerInfo { peer_id, addr } => {
                Ok(Some(PeerInfo { peer_id, public_addr: addr }))
            }
            SignalingMessage::PeerNotFound { .. } => Ok(None),
            SignalingMessage::Error { message } => {
                anyhow::bail!("Signaling error: {}", message)
            }
            _ => Ok(None),
        }
    }

    pub async fn connect_to_peer(
        &mut self,
        target_peer_id: &str,
        local_udp_addr: &SocketAddr,
        congestion: CongestionControl,
    ) -> Result<P2PConnection> {
        let msg = SignalingMessage::ConnectRequest {
            from: self.peer_id.clone(),
            to: target_peer_id.to_string(),
        };
        self.signaling_tx.send(msg).await
            .map_err(|_| anyhow::anyhow!("Failed to send connect request"))?;

        let response = tokio::time::timeout(
            Duration::from_secs(10),
            self.signaling_rx.recv(),
        )
        .await
        .context("Timeout waiting for connect response")?
        .context("Signaling channel closed")?;

        let target_addr = match response {
            SignalingMessage::ConnectResponse { addr, .. } => {
                addr.parse::<SocketAddr>().context("Invalid peer address")?
            }
            SignalingMessage::PeerNotFound { peer_id } => {
                anyhow::bail!("Peer not found: {}", peer_id)
            }
            SignalingMessage::Error { message } => {
                anyhow::bail!("Signaling error: {}", message)
            }
            _ => anyhow::bail!("Unexpected response"),
        };

        let udp_socket = UdpSocket::bind("0.0.0.0:0").await?;
        let local_udp = udp_socket.local_addr()?;

        let hole_msg = SignalingMessage::HolePunch {
            from: self.peer_id.clone(),
            to: target_peer_id.to_string(),
            addr: local_udp.to_string(),
        };
        self.signaling_tx.send(hole_msg).await.ok();

        let hole_response = tokio::time::timeout(
            Duration::from_secs(5),
            self.signaling_rx.recv(),
        )
        .await
        .ok();

        let target_udp_addr = match hole_response {
            Some(Some(SignalingMessage::HolePunch { addr, .. })) => {
                addr.parse::<SocketAddr>().ok().unwrap_or(target_addr)
            }
            _ => target_addr,
        };

        let p2p_conn = attempt_hole_punching(
            udp_socket,
            local_udp,
            target_udp_addr,
            congestion,
        ).await?;

        Ok(p2p_conn)
    }

    pub fn signaling_url(&self) -> &str {
        &self.signaling_url
    }

    pub fn peer_id(&self) -> &str {
        &self.peer_id
    }
}

pub struct P2PConnection {
    pub remote_addr: SocketAddr,
    pub local_addr: SocketAddr,
    pub udp_socket: Arc<UdpSocket>,
    pub congestion: CongestionControl,
}

async fn attempt_hole_punching(
    socket: UdpSocket,
    local_addr: SocketAddr,
    target_addr: SocketAddr,
    congestion: CongestionControl,
) -> Result<P2PConnection> {
    socket.connect(target_addr).await?;

    let punch_payload = b"QUIC_P2P_PUNCH";
    for _ in 0..5 {
        let _ = socket.send(punch_payload).await;
        tokio::time::sleep(Duration::from_millis(100)).await;
    }

    let mut buf = vec![0u8; 1024];
    let punch_success = tokio::time::timeout(
        Duration::from_secs(3),
        socket.recv(&mut buf),
    )
    .await
    .is_ok();

    if !punch_success {
        println!("  Hole punching did not get response, trying QUIC connection anyway...");
    } else {
        println!("  Hole punching successful!");
    }

    Ok(P2PConnection {
        remote_addr: target_addr,
        local_addr,
        udp_socket: Arc::new(socket),
        congestion,
    })
}

pub async fn wait_for_peer_connection(
    signaling_url: &str,
    peer_id: &str,
    congestion: CongestionControl,
) -> Result<P2PConnection> {
    let (ws_stream, _) = connect_async(signaling_url.to_string()).await
        .context("Failed to connect to signaling server")?;
    let (mut ws_write, mut ws_read) = ws_stream.split();

    let register_msg = SignalingMessage::Register {
        peer_id: peer_id.to_string(),
    };
    ws_write.send(Message::Text(serde_json::to_string(&register_msg)?.into())).await?;

    println!("  Registered as '{}', waiting for connection...", peer_id);

    let udp_socket = UdpSocket::bind("0.0.0.0:0").await?;
    let local_udp = udp_socket.local_addr()?;

    loop {
        tokio::select! {
            msg = ws_read.next() => {
                match msg {
                    Some(Ok(Message::Text(text))) => {
                        if let Ok(sig_msg) = serde_json::from_str::<SignalingMessage>(&text) {
                            match sig_msg {
                                SignalingMessage::ConnectRequest { from, .. } => {
                                    let hole_msg = SignalingMessage::HolePunch {
                                        from: peer_id.to_string(),
                                        to: from.clone(),
                                        addr: local_udp.to_string(),
                                    };
                                    ws_write.send(Message::Text(
                                        serde_json::to_string(&hole_msg)?.into()
                                    )).await?;
                                }
                                SignalingMessage::HolePunch { from, addr, .. } => {
                                    let target_addr: SocketAddr = addr.parse()?;
                                    udp_socket.connect(target_addr).await?;

                                    let punch_payload = b"QUIC_P2P_PUNCH";
                                    for _ in 0..5 {
                                        let _ = udp_socket.send(punch_payload).await;
                                        tokio::time::sleep(Duration::from_millis(100)).await;
                                    }

                                    return Ok(P2PConnection {
                                        remote_addr: target_addr,
                                        local_addr: local_udp,
                                        udp_socket: Arc::new(udp_socket),
                                        congestion,
                                    });
                                }
                                _ => {}
                            }
                        }
                    }
                    _ => {}
                }
            }
        }
    }
}
