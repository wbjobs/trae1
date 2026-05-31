use std::fs::{self, File};
use std::io::{BufReader, Read};
use std::path::{Path, PathBuf};
use std::sync::Arc;

use anyhow::{Context, Result};
use bytes::Bytes;
use quinn::{ClientConfig, Endpoint, RecvStream, SendStream, TransportConfig};
use rustls::{
    pki_types::{CertificateDer, ServerName, UnixTime},
};

use crate::cli::CongestionControl;
use crate::hash::Sha256Writer;
use crate::progress::TransferProgress;
use crate::protocol::*;
use crate::tar_util;

pub async fn run_client(
    ip: &str,
    port: u16,
    files: Vec<PathBuf>,
    temp_dir: Option<PathBuf>,
    congestion: CongestionControl,
) -> Result<()> {
    if files.is_empty() {
        anyhow::bail!("No files specified");
    }
    if files.len() > MAX_FILES {
        anyhow::bail!("Too many files. Maximum is {}", MAX_FILES);
    }

    for f in &files {
        if !f.exists() {
            anyhow::bail!("File not found: {:?}", f);
        }
        let size = fs::metadata(f)?.len();
        if size > MAX_FILE_SIZE {
            anyhow::bail!("File too large: {:?} ({} bytes)", f, size);
        }
    }

    let crypto = rustls::crypto::CryptoProvider::get_default()
        .context("Failed to get default crypto provider")?
        .clone();

    let rustls_config = rustls::ClientConfig::builder_with_provider(crypto)
        .with_protocol_versions(&[&rustls::version::TLS13])
        .context("TLS 1.3 not supported")?
        .dangerous()
        .with_custom_certificate_verifier(Arc::new(AcceptAllCertVerifier))
        .with_no_client_auth();

    let quic_config = quinn::crypto::rustls::QuicClientConfig::try_from(Arc::new(rustls_config))
        .context("Failed to create QUIC client config")?;

    let mut client_config = ClientConfig::new(Arc::new(quic_config));

    let mut transport_config = TransportConfig::default();
    apply_congestion_control(&mut transport_config, congestion);
    client_config.transport_config(Arc::new(transport_config));

    let mut endpoint = Endpoint::client("0.0.0.0:0".parse()?)?;
    endpoint.set_default_client_config(client_config);

    let server_addr = format!("{}:{}", ip, port);
    println!("Connecting to {}...", server_addr);

    let connection = endpoint
        .connect(server_addr.parse()?, "quic-transfer")?
        .await?;

    println!("Connected to {}", connection.remote_address());

    if files.len() == 1 && !files[0].is_dir() {
        send_single_file(&connection, &files[0]).await?;
    } else {
        send_multiple_files(&connection, &files, temp_dir.as_deref()).await?;
    }

    connection.close(0u32.into(), b"done");
    endpoint.wait_idle().await;

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

#[derive(Debug)]
struct AcceptAllCertVerifier;

impl rustls::client::danger::ServerCertVerifier for AcceptAllCertVerifier {
    fn verify_server_cert(
        &self,
        _end_entity: &CertificateDer<'_>,
        _intermediates: &[CertificateDer<'_>],
        _server_name: &ServerName<'_>,
        _ocsp_response: &[u8],
        _now: UnixTime,
    ) -> std::result::Result<rustls::client::danger::ServerCertVerified, rustls::Error> {
        Ok(rustls::client::danger::ServerCertVerified::assertion())
    }

    fn verify_tls12_signature(
        &self,
        _message: &[u8],
        _cert: &CertificateDer<'_>,
        _dss: &rustls::DigitallySignedStruct,
    ) -> std::result::Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        Ok(rustls::client::danger::HandshakeSignatureValid::assertion())
    }

    fn verify_tls13_signature(
        &self,
        _message: &[u8],
        _cert: &CertificateDer<'_>,
        _dss: &rustls::DigitallySignedStruct,
    ) -> std::result::Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        Ok(rustls::client::danger::HandshakeSignatureValid::assertion())
    }

    fn supported_verify_schemes(&self) -> Vec<rustls::SignatureScheme> {
        vec![
            rustls::SignatureScheme::RSA_PKCS1_SHA256,
            rustls::SignatureScheme::RSA_PKCS1_SHA384,
            rustls::SignatureScheme::RSA_PKCS1_SHA512,
            rustls::SignatureScheme::ECDSA_NISTP256_SHA256,
            rustls::SignatureScheme::ED25519,
        ]
    }
}

async fn send_single_file(connection: &quinn::Connection, file_path: &Path) -> Result<()> {
    let file_name = file_path
        .file_name()
        .unwrap()
        .to_string_lossy()
        .to_string();
    let file_size = fs::metadata(file_path)?.len();

    println!("Sending: {} ({} bytes)", file_name, file_size);

    let (mut send, mut recv) = connection.open_bi().await?;

    let sha256 = crate::hash::compute_file_sha256(file_path)?;
    println!("SHA256: {}", sha256);

    let meta = FileMeta {
        file_id: 1,
        file_name,
        file_size,
        is_tar: false,
        num_files: None,
        sha256: Some(sha256.clone()),
    };

    send_file_via_stream(&mut send, &mut recv, &meta, file_path, &sha256).await?;

    Ok(())
}

async fn send_multiple_files(
    connection: &quinn::Connection,
    files: &[PathBuf],
    temp_dir: Option<&Path>,
) -> Result<()> {
    let tmp = temp_dir.unwrap_or_else(|| Path::new("."));
    fs::create_dir_all(tmp)?;

    let tar_path = tmp.join("quic-transfer-bundle.tar");
    println!("Creating tar bundle with {} files...", files.len());

    let tar_size = tar_util::pack_files_to_tar(files, &tar_path)?;
    println!("Tar bundle size: {} bytes", tar_size);

    let sha256 = crate::hash::compute_file_sha256(&tar_path)?;
    println!("Tar SHA256: {}", sha256);

    let (mut send, mut recv) = connection.open_bi().await?;

    let meta = FileMeta {
        file_id: 1,
        file_name: "quic-transfer-bundle.tar".to_string(),
        file_size: tar_size,
        is_tar: true,
        num_files: Some(files.len() as u32),
        sha256: Some(sha256.clone()),
    };

    send_file_via_stream(&mut send, &mut recv, &meta, &tar_path, &sha256).await?;

    fs::remove_file(&tar_path).ok();

    Ok(())
}

async fn send_file_via_stream(
    send: &mut SendStream,
    recv: &mut RecvStream,
    meta: &FileMeta,
    file_path: &Path,
    sha256: &str,
) -> Result<()> {
    let meta_bytes = encode_meta(meta)?;
    send.write_all(&meta_bytes).await?;

    let mut progress = TransferProgress::new(meta.file_size, &format!("Sending: {}", meta.file_name));

    let mut send_offset: u64 = 0;

    let resume_result = tokio::time::timeout(
        std::time::Duration::from_millis(500),
        read_message(recv),
    )
    .await;

    if let Ok(Ok(msg)) = resume_result {
        match msg {
            Message::ResumeRequest(req) => {
                if req.offset > 0 && req.offset < meta.file_size {
                    println!("Resume requested from offset: {}", req.offset);
                    let ack = ResumeAck {
                        file_id: meta.file_id,
                        offset: req.offset,
                        accepted: true,
                    };
                    send.write_all(&encode_resume_ack(&ack)?).await?;
                    send_offset = req.offset;
                    progress.update(send_offset);
                } else {
                    let ack = ResumeAck {
                        file_id: meta.file_id,
                        offset: 0,
                        accepted: false,
                    };
                    send.write_all(&encode_resume_ack(&ack)?).await?;
                }
            }
            Message::Ack { offset, .. } => {
                if offset < meta.file_size {
                    send_offset = offset;
                    progress.update(send_offset);
                }
            }
            _ => {}
        }
    }

    let file = File::open(file_path)?;
    let mut reader = BufReader::with_capacity(CHUNK_SIZE, file);

    if send_offset > 0 {
        std::io::Seek::seek(&mut reader, std::io::SeekFrom::Start(send_offset))?;
    }

    let mut buffer = vec![0u8; CHUNK_SIZE];
    let mut hasher = Sha256Writer::new();

    if send_offset > 0 {
        let mut pre_reader = BufReader::with_capacity(CHUNK_SIZE, File::open(file_path)?);
        let mut pre_buffer = vec![0u8; CHUNK_SIZE];
        let mut hashed: u64 = 0;
        while hashed < send_offset {
            let to_read = std::cmp::min(CHUNK_SIZE as u64, send_offset - hashed) as usize;
            let n = pre_reader.read(&mut pre_buffer[..to_read])?;
            if n == 0 {
                break;
            }
            hasher.update(&pre_buffer[..n]);
            hashed += n as u64;
        }
    }

    loop {
        let n = reader.read(&mut buffer)?;
        if n == 0 {
            break;
        }

        let data = Bytes::copy_from_slice(&buffer[..n]);
        let file_data = FileData {
            file_id: meta.file_id,
            offset: send_offset,
            data: data.clone(),
        };

        send.write_all(&encode_data(&file_data)).await?;
        hasher.update(&data);
        send_offset += n as u64;
        progress.increment(n as u64);

        let _ = tokio::time::timeout(
            std::time::Duration::from_millis(100),
            read_message(recv),
        )
        .await;
    }

    let computed_hash = hasher.finalize();

    if computed_hash != sha256 {
        eprintln!(
            "WARNING: Local computed hash ({}) differs from expected ({}). Continuing anyway.",
            computed_hash, sha256
        );
    }

    let done = FileDone {
        file_id: meta.file_id,
        sha256: sha256.to_string(),
    };
    send.write_all(&encode_done(&done)?).await?;

    loop {
        let msg = read_message(recv).await?;
        match msg {
            Message::ResumeRequest(req) => {
                if req.offset == 0 {
                    println!("Retransmitting entire file...");
                    let ack = ResumeAck {
                        file_id: meta.file_id,
                        offset: 0,
                        accepted: true,
                    };
                    send.write_all(&encode_resume_ack(&ack)?).await?;

                    let file = File::open(file_path)?;
                    let mut reader = BufReader::with_capacity(CHUNK_SIZE, file);
                    let mut offset = 0u64;
                    let mut buffer = vec![0u8; CHUNK_SIZE];
                    let mut hasher = Sha256Writer::new();

                    let mut re_progress = TransferProgress::new(
                        meta.file_size,
                        &format!("Retransmitting: {}", meta.file_name),
                    );

                    loop {
                        let n = reader.read(&mut buffer)?;
                        if n == 0 {
                            break;
                        }

                        let data = Bytes::copy_from_slice(&buffer[..n]);
                        let file_data = FileData {
                            file_id: meta.file_id,
                            offset,
                            data: data.clone(),
                        };

                        send.write_all(&encode_data(&file_data)).await?;
                        hasher.update(&data);
                        offset += n as u64;
                        re_progress.increment(n as u64);

                        let _ = tokio::time::timeout(
                            std::time::Duration::from_millis(100),
                            read_message(recv),
                        )
                        .await;
                    }

                    let re_done = FileDone {
                        file_id: meta.file_id,
                        sha256: sha256.to_string(),
                    };
                    send.write_all(&encode_done(&re_done)?).await?;
                }
            }
            Message::TransferComplete => {
                progress.finish();
                println!("File transfer complete: {}", meta.file_name);
                break;
            }
            Message::Error(e) => {
                progress.finish_with_error(&format!("Error: {}", e));
                anyhow::bail!("Peer error: {}", e);
            }
            _ => {}
        }
    }

    Ok(())
}

pub async fn run_client_p2p(
    local_id: &str,
    target_peer_id: &str,
    signaling_url: &str,
    files: Vec<PathBuf>,
    temp_dir: Option<PathBuf>,
    congestion: CongestionControl,
) -> Result<()> {
    if files.is_empty() {
        anyhow::bail!("No files specified");
    }
    if files.len() > MAX_FILES {
        anyhow::bail!("Too many files. Maximum is {}", MAX_FILES);
    }

    for f in &files {
        if !f.exists() {
            anyhow::bail!("File not found: {:?}", f);
        }
        let size = fs::metadata(f)?.len();
        if size > MAX_FILE_SIZE {
            anyhow::bail!("File too large: {:?} ({} bytes)", f, size);
        }
    }

    println!("Connecting to signaling server...");
    let mut p2p_client = crate::p2p::P2PClient::connect(signaling_url, local_id).await?;

    println!("Querying peer '{}'...", target_peer_id);
    let peer_info = p2p_client.query_peer(target_peer_id).await?;

    match peer_info {
        Some(info) => {
            println!("Found peer: {} ({})", info.peer_id, info.public_addr);
        }
        None => {
            anyhow::bail!("Peer '{}' not found or offline", target_peer_id);
        }
    }

    println!("Establishing P2P connection to '{}'...", target_peer_id);
    let local_udp: std::net::SocketAddr = "0.0.0.0:0".parse()?;
    let p2p_conn = p2p_client.connect_to_peer(target_peer_id, &local_udp, congestion).await?;

    println!("P2P connection established!");
    println!("  Remote: {}", p2p_conn.remote_addr);
    println!("  Local:  {}", p2p_conn.local_addr);

    let crypto = rustls::crypto::CryptoProvider::get_default()
        .context("Failed to get default crypto provider")?
        .clone();

    let rustls_config = rustls::ClientConfig::builder_with_provider(crypto)
        .with_protocol_versions(&[&rustls::version::TLS13])
        .context("TLS 1.3 not supported")?
        .dangerous()
        .with_custom_certificate_verifier(Arc::new(AcceptAllCertVerifier))
        .with_no_client_auth();

    let quic_config = quinn::crypto::rustls::QuicClientConfig::try_from(Arc::new(rustls_config))
        .context("Failed to create QUIC client config")?;

    let mut client_config = ClientConfig::new(Arc::new(quic_config));

    let mut transport_config = TransportConfig::default();
    apply_congestion_control(&mut transport_config, p2p_conn.congestion);
    client_config.transport_config(Arc::new(transport_config));

    let mut endpoint = Endpoint::client("0.0.0.0:0".parse()?)?;
    endpoint.set_default_client_config(client_config);

    println!("Connecting QUIC over P2P channel...");
    let connection = endpoint
        .connect(p2p_conn.remote_addr, "quic-transfer")?
        .await?;

    println!("QUIC connected to {}", connection.remote_address());

    if files.len() == 1 && !files[0].is_dir() {
        send_single_file(&connection, &files[0]).await?;
    } else {
        send_multiple_files(&connection, &files, temp_dir.as_deref()).await?;
    }

    connection.close(0u32.into(), b"done");
    endpoint.wait_idle().await;

    Ok(())
}
