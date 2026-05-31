use std::collections::HashMap;
use std::sync::Arc;

use anyhow::Result;
use clap::Parser;
use futures::{SinkExt, StreamExt};
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use tokio::net::{TcpListener, TcpStream, UdpSocket};
use tokio_tungstenite::{accept_async, tungstenite::Message};

#[derive(Parser)]
#[command(name = "quic-signaling", version, about = "QUIC P2P Signaling Server")]
struct Cli {
    #[arg(short, long, default_value = "0.0.0.0", help = "Bind address")]
    bind: String,

    #[arg(short, long, default_value_t = 8888, help = "WebSocket port")]
    port: u16,

    #[arg(long, default_value_t = 9000, help = "UDP relay port")]
    relay_port: u16,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
enum SignalingMessage {
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

struct Peer {
    peer_id: String,
    addr: String,
    udp_addr: Option<String>,
}

type Peers = Arc<Mutex<HashMap<String, Peer>>>;

#[tokio::main]
async fn main() -> Result<()> {
    let cli = Cli::parse();

    let peers: Peers = Arc::new(Mutex::new(HashMap::new()));

    let ws_addr = format!("{}:{}", cli.bind, cli.port);
    let relay_addr = format!("{}:{}", cli.bind, cli.relay_port);

    println!("=== QUIC P2P Signaling Server ===");
    println!("WebSocket: ws://{}", ws_addr);
    println!("UDP Relay:   udp://{}", relay_addr);
    println!("================================");

    let peers_clone = peers.clone();
    let relay_addr_clone = relay_addr.clone();
    tokio::spawn(async move {
        if let Err(e) = run_udp_relay(&relay_addr_clone, peers_clone).await {
            eprintln!("UDP relay error: {:?}", e);
        }
    });

    let listener = TcpListener::bind(&ws_addr).await?;
    println!("Waiting for WebSocket connections...");

    while let Ok((stream, addr)) = listener.accept().await {
        let peers = peers.clone();
        tokio::spawn(async move {
            if let Err(e) = handle_ws_client(stream, addr.to_string(), peers).await {
                eprintln!("Client error ({}): {:?}", addr, e);
            }
        });
    }

    Ok(())
}

async fn handle_ws_client(
    stream: TcpStream,
    addr: String,
    peers: Peers,
) -> Result<()> {
    println!("New WebSocket connection from: {}", addr);

    let mut ws_stream = accept_async(stream).await?;

    let mut my_peer_id: Option<String> = None;

    while let Some(msg) = ws_stream.next().await {
        let msg = msg?;

        match msg {
            Message::Text(text) => {
                let parsed: Result<SignalingMessage, _> = serde_json::from_str(&text);
                match parsed {
                    Ok(sig_msg) => {
                        let response = handle_signaling_message(
                            sig_msg,
                            &addr,
                            &mut my_peer_id,
                            &peers,
                        ).await;

                        if let Some(resp) = response {
                            let resp_json = serde_json::to_string(&resp)?;
                            ws_stream.send(Message::Text(resp_json.into())).await?;
                        }
                    }
                    Err(e) => {
                        let err = SignalingMessage::Error {
                            message: format!("Invalid message: {}", e),
                        };
                        let err_json = serde_json::to_string(&err)?;
                        ws_stream.send(Message::Text(err_json.into())).await?;
                    }
                }
            }
            Message::Binary(_) => {}
            Message::Close(_) => break,
            _ => {}
        }
    }

    if let Some(pid) = &my_peer_id {
        peers.lock().remove(pid);
        println!("Peer unregistered: {} ({})", pid, addr);
    }

    Ok(())
}

async fn handle_signaling_message(
    msg: SignalingMessage,
    addr: &str,
    my_peer_id: &mut Option<String>,
    peers: &Peers,
) -> Option<SignalingMessage> {
    match msg {
        SignalingMessage::Register { peer_id } => {
            let mut p = peers.lock();
            p.insert(peer_id.clone(), Peer {
                peer_id: peer_id.clone(),
                addr: addr.to_string(),
                udp_addr: None,
            });
            drop(p);

            *my_peer_id = Some(peer_id.clone());
            println!("Peer registered: {} ({})", peer_id, addr);

            Some(SignalingMessage::PeerInfo {
                peer_id: peer_id.clone(),
                addr: addr.to_string(),
            })
        }

        SignalingMessage::PeerQuery { peer_id } => {
            let p = peers.lock();
            match p.get(&peer_id) {
                Some(peer) => Some(SignalingMessage::PeerInfo {
                    peer_id: peer.peer_id.clone(),
                    addr: peer.addr.clone(),
                }),
                None => Some(SignalingMessage::PeerNotFound { peer_id }),
            }
        }

        SignalingMessage::PeerListRequest => {
            let p = peers.lock();
            let ids: Vec<String> = p.keys().cloned().collect();
            Some(SignalingMessage::PeerList { peers: ids })
        }

        SignalingMessage::ConnectRequest { from, to } => {
            let p = peers.lock();
            if let Some(target) = p.get(&to) {
                println!("Connect request: {} -> {} ({})", from, to, target.addr);
                Some(SignalingMessage::ConnectResponse {
                    from: to.clone(),
                    addr: target.addr.clone(),
                })
            } else {
                Some(SignalingMessage::PeerNotFound { peer_id: to })
            }
        }

        SignalingMessage::HolePunch { from, to, addr } => {
            println!("Hole punch info: {} ({}) -> {}", from, addr, to);

            let mut p = peers.lock();
            if let Some(peer) = p.get_mut(&from) {
                peer.udp_addr = Some(addr);
            }

            if let Some(target) = p.get(&to) {
                let target_udp = target.udp_addr.clone().unwrap_or_default();

                if let Some(sender) = p.get(&from) {
                    let _sender_udp = sender.udp_addr.clone().unwrap_or_default();
                    return Some(SignalingMessage::HolePunch {
                        from: to.clone(),
                        to: from.clone(),
                        addr: target_udp,
                    });
                }
            }

            Some(SignalingMessage::Error {
                message: "Peer not found for hole punch".to_string(),
            })
        }

        _ => None,
    }
}

async fn run_udp_relay(addr: &str, peers: Peers) -> Result<()> {
    let socket = UdpSocket::bind(addr).await?;
    let mut buf = vec![0u8; 65536];

    loop {
        let (len, _src) = socket.recv_from(&mut buf).await?;

        if len < 4 {
            continue;
        }

        let marker = &buf[0..4];
        if marker != b"QLYT" {
            continue;
        }

        if len < 8 {
            continue;
        }

        let id_len = u32::from_be_bytes([buf[4], buf[5], buf[6], buf[7]]) as usize;
        if len < 8 + id_len + 4 {
            continue;
        }

        let target_id = String::from_utf8_lossy(&buf[8..8 + id_len]).to_string();
        let data = &buf[8 + id_len..len];

        let dest_addr = {
            let p = peers.lock();
            p.get(&target_id)
                .and_then(|t| t.udp_addr.clone())
                .and_then(|a| a.parse::<std::net::SocketAddr>().ok())
        };

        if let Some(dest) = dest_addr {
            let _ = socket.send_to(data, dest).await;
        }
    }
}
