use std::fs::{self, File};
use std::io::Write;
use std::net::SocketAddr;
use std::path::{Path, PathBuf};
use std::sync::Arc;

use anyhow::Result;
use quinn::{Endpoint, RecvStream, SendStream, ServerConfig, TransportConfig};
use rustls::pki_types::{CertificateDer, PrivateKeyDer, PrivatePkcs8KeyDer};

use crate::cli::CongestionControl;
use crate::hash::Sha256Writer;
use crate::progress::TransferProgress;
use crate::protocol::*;

pub async fn run_server(
    bind_addr: &str,
    port: u16,
    output_dir: PathBuf,
    cert_path: Option<PathBuf>,
    key_path: Option<PathBuf>,
    congestion: CongestionControl,
) -> Result<()> {
    fs::create_dir_all(&output_dir)?;

    let (certs, key) = if let (Some(cert), Some(key)) = (cert_path, key_path) {
        let cert_chain = read_certs(&cert)?;
        let priv_key = read_key(&key)?;
        (cert_chain, priv_key)
    } else {
        println!("Generating self-signed certificate...");
        generate_cert()?
    };

    let mut server_config = ServerConfig::with_single_cert(certs, key)?;

    let mut transport_config = TransportConfig::default();
    apply_congestion_control(&mut transport_config, congestion);
    server_config.transport_config(Arc::new(transport_config));

    let addr = format!("{}:{}", bind_addr, port).parse()?;
    let endpoint = Endpoint::server(server_config, addr)?;

    println!("Server listening on {}", addr);
    println!("Use the following to connect: quic-transfer send --ip <your_ip> --port {} --congestion {:?}", port, congestion);

    while let Some(incoming) = endpoint.accept().await {
        let remote_addr = incoming.remote_address();
        println!("Incoming connection from: {}", remote_addr);

        let output_dir = output_dir.clone();
        tokio::spawn(async move {
            let conn = match incoming.accept() {
                Ok(conn) => conn,
                Err(e) => {
                    eprintln!("Failed to accept connection from {}: {:?}", remote_addr, e);
                    return;
                }
            };
            if let Err(e) = handle_connection(conn, output_dir).await {
                eprintln!("Error handling connection from {}: {:?}", remote_addr, e);
            }
        });
    }

    endpoint.wait_idle().await;
    Ok(())
}

async fn handle_connection(conn: quinn::Connecting, output_dir: PathBuf) -> Result<()> {
    let connection = conn.await?;

    loop {
        tokio::select! {
            stream = connection.accept_bi() => {
                let (send, recv) = stream?;
                let output_dir = output_dir.clone();
                tokio::spawn(async move {
                    if let Err(e) = handle_stream(send, recv, output_dir).await {
                        eprintln!("Stream error: {:?}", e);
                    }
                });
            }
            _ = connection.closed() => {
                println!("Connection closed by peer");
                break;
            }
        }
    }

    Ok(())
}

async fn handle_stream(
    mut send: SendStream,
    mut recv: RecvStream,
    output_dir: PathBuf,
) -> Result<()> {
    let msg = read_message(&mut recv).await?;

    match msg {
        Message::FileMeta(meta) => {
            handle_file_receive(&mut send, &mut recv, meta, &output_dir).await?;
        }
        _ => {
            let err_msg = "Expected FileMeta message";
            send.write_all(&encode_error(err_msg)).await?;
            anyhow::bail!(err_msg);
        }
    }

    Ok(())
}

async fn handle_file_receive(
    send: &mut SendStream,
    recv: &mut RecvStream,
    meta: FileMeta,
    output_dir: &Path,
) -> Result<()> {
    let file_path = output_dir.join(&meta.file_name);

    let (existing_size, mut file) = if file_path.exists() {
        let existing_size = fs::metadata(&file_path)?.len();
        if existing_size < meta.file_size {
            println!(
                "Found partial file: {} ({} bytes), resuming...",
                meta.file_name, existing_size
            );

            let resume_req = ResumeRequest {
                file_id: meta.file_id,
                offset: existing_size,
            };
            send.write_all(&encode_resume_request(&resume_req)?).await?;

            let msg = read_message(recv).await?;
            match msg {
                Message::ResumeAck(ack) => {
                    if !ack.accepted {
                        let msg = "Resume rejected, restarting from beginning";
                        println!("{}", msg);
                        let file = File::create(&file_path)?;
                        (0u64, file)
                    } else {
                        let file = fs::OpenOptions::new()
                            .append(true)
                            .open(&file_path)?;
                        (existing_size, file)
                    }
                }
                _ => {
                    let file = File::create(&file_path)?;
                    (0u64, file)
                }
            }
        } else if existing_size == meta.file_size {
            println!("File already exists and complete: {}", meta.file_name);
            if let Some(ref expected_hash) = meta.sha256 {
                let actual_hash = crate::hash::compute_file_sha256(&file_path)?;
                if actual_hash == *expected_hash {
                    println!("SHA256 verified, skipping.");
                    send.write_all(&encode_transfer_complete()).await?;
                    return Ok(());
                }
            }
            let file = File::create(&file_path)?;
            (0u64, file)
        } else {
            let file = File::create(&file_path)?;
            (0u64, file)
        }
    } else {
        if let Some(parent) = file_path.parent() {
            fs::create_dir_all(parent)?;
        }
        let file = File::create(&file_path)?;
        (0u64, file)
    };

    let mut hasher = Sha256Writer::new();
    let mut progress = TransferProgress::new(meta.file_size, &format!("Receiving: {}", meta.file_name));
    let mut received: u64 = existing_size;

    progress.update(existing_size);

    loop {
        let msg = read_message(recv).await?;
        match msg {
            Message::FileData(data) => {
                if data.file_id != meta.file_id {
                    continue;
                }
                if data.offset < received {
                    let overlap = (received - data.offset) as usize;
                    if overlap < data.data.len() {
                        let new_data = &data.data[overlap..];
                        file.write_all(new_data)?;
                        hasher.update(new_data);
                        received = data.offset + data.data.len() as u64;
                        progress.increment(new_data.len() as u64);
                    }
                } else if data.offset == received {
                    file.write_all(&data.data)?;
                    hasher.update(&data.data);
                    received += data.data.len() as u64;
                    progress.increment(data.data.len() as u64);
                } else {
                    continue;
                }

                send.write_all(&encode_ack(meta.file_id, received)).await?;
            }
            Message::FileDone(done) => {
                file.flush()?;
                drop(file);

                let actual_hash = crate::hash::compute_file_sha256(&file_path)?;
                progress.finish();

                if actual_hash == done.sha256 {
                    println!(
                        "File received successfully: {} (SHA256: {})",
                        meta.file_name, actual_hash
                    );
                } else {
                    println!(
                        "SHA256 mismatch for {}: expected {}, got {}. Requesting retransfer...",
                        meta.file_name, done.sha256, actual_hash
                    );

                    fs::remove_file(&file_path)?;
                    let resume_req = ResumeRequest {
                        file_id: meta.file_id,
                        offset: 0,
                    };
                    send.write_all(&encode_resume_request(&resume_req)?).await?;

                    let msg = read_message(recv).await?;
                    match msg {
                        Message::ResumeAck(ack) => {
                            if ack.accepted && ack.offset == 0 {
                                let mut new_file = File::create(&file_path)?;
                                let mut new_hasher = Sha256Writer::new();
                                let mut new_progress =
                                    TransferProgress::new(meta.file_size, &format!("Retransmitting: {}", meta.file_name));
                                let mut new_received: u64 = 0;

                                loop {
                                    let msg = read_message(recv).await?;
                                    match msg {
                                        Message::FileData(data) => {
                                            if data.offset == new_received {
                                                new_file.write_all(&data.data)?;
                                                new_hasher.update(&data.data);
                                                new_received += data.data.len() as u64;
                                                new_progress.increment(data.data.len() as u64);
                                            }
                                            send.write_all(&encode_ack(meta.file_id, new_received))
                                                .await?;
                                        }
                                        Message::FileDone(done) => {
                                            new_file.flush()?;
                                            drop(new_file);
                                            let actual_hash =
                                                crate::hash::compute_file_sha256(&file_path)?;
                                            new_progress.finish();
                                            if actual_hash == done.sha256 {
                                                println!(
                                                    "File retransmitted successfully: {} (SHA256: {})",
                                                    meta.file_name, actual_hash
                                                );
                                            } else {
                                                println!(
                                                    "SHA256 mismatch after retransmission. Giving up."
                                                );
                                                fs::remove_file(&file_path)?;
                                            }
                                            break;
                                        }
                                        Message::Error(e) => {
                                            anyhow::bail!("Peer error: {}", e);
                                        }
                                        _ => {}
                                    }
                                }
                            }
                        }
                        _ => {}
                    }
                }

                send.write_all(&encode_transfer_complete()).await?;
                break;
            }
            Message::Error(e) => {
                progress.finish_with_error(&format!("Error: {}", e));
                anyhow::bail!("Peer error: {}", e);
            }
            _ => {}
        }
    }

    if meta.is_tar {
        println!("Unpacking tar bundle...");
        match crate::tar_util::unpack_tar_to_dir(&file_path, output_dir) {
            Ok(paths) => {
                println!("Extracted {} files", paths.len());
                fs::remove_file(&file_path).ok();
            }
            Err(e) => {
                eprintln!("Failed to unpack tar: {:?}", e);
            }
        }
    }

    Ok(())
}

fn apply_congestion_control(config: &mut TransportConfig, congestion: CongestionControl) {
    let factory: Arc<dyn quinn::congestion::ControllerFactory + Send + Sync> = match congestion {
        CongestionControl::Bbr => {
            println!("Using BBR congestion control (optimized for high packet loss)");
            Arc::new(quinn::congestion::BbrConfig::default())
        }
        CongestionControl::Cubic => {
            println!("Using CUBIC congestion control (default, widely used for TCP)");
            Arc::new(quinn::congestion::CubicConfig::default())
        }
        CongestionControl::Reno => {
            println!("Using NewReno congestion control (traditional TCP)");
            Arc::new(quinn::congestion::NewRenoConfig::default())
        }
    };
    config.congestion_controller_factory(factory);
}

fn generate_cert() -> Result<(Vec<CertificateDer<'static>>, PrivateKeyDer<'static>)> {
    let cert = rcgen::generate_simple_self_signed(vec!["localhost".to_string()])?;
    let cert_der = CertificateDer::from(cert.cert);
    let key_der = PrivateKeyDer::Pkcs8(PrivatePkcs8KeyDer::from(cert.key_pair.serialize_der()));
    Ok((vec![cert_der], key_der))
}

fn read_certs(path: &Path) -> Result<Vec<CertificateDer<'static>>> {
    let certs: Vec<CertificateDer> = rustls_pemfile::certs(&mut std::io::BufReader::new(
        std::fs::File::open(path)?,
    ))
    .collect::<Result<Vec<_>, _>>()?;
    Ok(certs)
}

fn read_key(path: &Path) -> Result<PrivateKeyDer<'static>> {
    let key = rustls_pemfile::private_key(&mut std::io::BufReader::new(
        std::fs::File::open(path)?,
    ))?
    .ok_or_else(|| anyhow::anyhow!("No private key found"))?;
    Ok(key)
}

pub async fn run_server_p2p(
    bind_addr: &str,
    port: u16,
    output_dir: PathBuf,
    cert_path: Option<PathBuf>,
    key_path: Option<PathBuf>,
    congestion: CongestionControl,
    peer_id: &str,
    signaling_url: &str,
) -> Result<()> {
    fs::create_dir_all(&output_dir)?;

    let (certs, key) = if let (Some(cert), Some(key)) = (cert_path, key_path) {
        let cert_chain = read_certs(&cert)?;
        let priv_key = read_key(&key)?;
        (cert_chain, priv_key)
    } else {
        println!("Generating self-signed certificate...");
        generate_cert()?
    };

    println!("Waiting for P2P connection as '{}'...", peer_id);
    println!("Send: quic-transfer send --peer-id {} --local-id <your_id> --signaling {}", peer_id, signaling_url);

    let p2p_conn = crate::p2p::wait_for_peer_connection(signaling_url, peer_id, congestion).await?;

    println!("P2P connection established!");
    println!("  Remote: {}", p2p_conn.remote_addr);
    println!("  Local:  {}", p2p_conn.local_addr);

    let mut server_config = ServerConfig::with_single_cert(certs, key)?;

    let mut transport_config = TransportConfig::default();
    apply_congestion_control(&mut transport_config, p2p_conn.congestion);
    server_config.transport_config(Arc::new(transport_config));

    let server_addr: SocketAddr = format!("{}:{}", bind_addr, port).parse()?;
    let endpoint = Endpoint::server(server_config, server_addr)?;

    println!("QUIC server ready on P2P channel");
    println!("Waiting for incoming QUIC connection...");

    if let Some(incoming) = endpoint.accept().await {
        let remote_addr = incoming.remote_address();
        println!("Incoming QUIC connection from: {}", remote_addr);

        let output_dir = output_dir.clone();
        tokio::spawn(async move {
            let conn = match incoming.accept() {
                Ok(conn) => conn,
                Err(e) => {
                    eprintln!("Failed to accept QUIC connection from {}: {:?}", remote_addr, e);
                    return;
                }
            };
            if let Err(e) = handle_connection(conn, output_dir).await {
                eprintln!("Error handling QUIC connection from {}: {:?}", remote_addr, e);
            }
        });
    }

    endpoint.wait_idle().await;
    Ok(())
}
