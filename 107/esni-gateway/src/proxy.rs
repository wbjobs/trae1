use crate::config::Config;
use crate::dnssec::DnssecVerifier;
use crate::ech::{EchDecryptor, EchFailureLogger};
use crate::fingerprint_manager::FingerprintManager;
use crate::metrics::METRICS;
use crate::router::{RoutingDecision, SniRouter};
use crate::tls_parser::{ClientHelloInfo, TlsParser};
use bytes::BytesMut;
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::Instant;
use thiserror::Error;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::mpsc;
use tokio::time::{timeout, Duration};
use tracing::{debug, info, warn};

#[derive(Error, Debug)]
pub enum ProxyError {
    #[error("Connection error: {0}")]
    ConnectionError(#[from] std::io::Error),
    #[error("TLS parse error: {0}")]
    TlsParseError(#[from] crate::tls_parser::TlsParseError),
    #[error("Timeout")]
    Timeout,
    #[error("Blocked by policy")]
    Blocked,
    #[error("DNSSEC verification failed")]
    DnssecFailed,
    #[error("ECH decryption failed")]
    EchDecryptionFailed,
}

pub struct Proxy {
    config: Arc<Config>,
    router: Arc<SniRouter>,
    dnssec_verifier: Arc<DnssecVerifier>,
    ech_decryptor: Option<Arc<EchDecryptor>>,
    ech_failure_logger: Option<Arc<EchFailureLogger>>,
    fingerprint_manager: Option<Arc<FingerprintManager>>,
}

impl Proxy {
    pub fn new(
        config: Arc<Config>,
        router: Arc<SniRouter>,
        dnssec_verifier: Arc<DnssecVerifier>,
        ech_decryptor: Option<Arc<EchDecryptor>>,
        ech_failure_logger: Option<Arc<EchFailureLogger>>,
        fingerprint_manager: Option<Arc<FingerprintManager>>,
    ) -> Self {
        Self {
            config,
            router,
            dnssec_verifier,
            ech_decryptor,
            ech_failure_logger,
            fingerprint_manager,
        }
    }

    pub async fn run(&self) -> Result<(), ProxyError> {
        let listener = TcpListener::bind(self.config.listen).await?;
        info!("ESNI Gateway listening on {}", self.config.listen);

        let (shutdown_tx, mut shutdown_rx) = mpsc::channel::<()>(1);

        tokio::spawn(async move {
            tokio::signal::ctrl_c()
                .await
                .expect("Failed to listen for ctrl-c");
            info!("Shutdown signal received");
            let _ = shutdown_tx.send(()).await;
        });

        loop {
            tokio::select! {
                accept_result = listener.accept() => {
                    match accept_result {
                        Ok((client_socket, client_addr)) => {
                            let proxy = self.clone();
                            let router = Arc::clone(&self.router);
                            let dnssec_verifier = Arc::clone(&self.dnssec_verifier);
                            let ech_decryptor = self.ech_decryptor.clone();
                            let ech_failure_logger = self.ech_failure_logger.clone();
                            let fingerprint_manager = self.fingerprint_manager.clone();
                            let config = Arc::clone(&self.config);

                            tokio::spawn(async move {
                                if let Err(e) = proxy
                                    .handle_connection(
                                        client_socket,
                                        client_addr,
                                        router,
                                        dnssec_verifier,
                                        ech_decryptor,
                                        ech_failure_logger,
                                        fingerprint_manager,
                                        config,
                                    )
                                    .await
                                {
                                    debug!("Connection error: {}", e);
                                }
                            });
                        }
                        Err(e) => {
                            warn!("Accept error: {}", e);
                        }
                    }
                }
                _ = shutdown_rx.recv() => {
                    info!("Shutting down proxy");
                    break;
                }
            }
        }

        Ok(())
    }

    async fn handle_connection(
        &self,
        mut client_socket: TcpStream,
        client_addr: SocketAddr,
        router: Arc<SniRouter>,
        dnssec_verifier: Arc<DnssecVerifier>,
        ech_decryptor: Option<Arc<EchDecryptor>>,
        ech_failure_logger: Option<Arc<EchFailureLogger>>,
        fingerprint_manager: Option<Arc<FingerprintManager>>,
        config: Arc<Config>,
    ) -> Result<(), ProxyError> {
        let start_time = Instant::now();
        METRICS.increment_connections();

        let result = self
            .handle_connection_inner(
                &mut client_socket,
                client_addr,
                router,
                dnssec_verifier,
                ech_decryptor,
                ech_failure_logger,
                fingerprint_manager,
                config,
            )
            .await;

        let duration = start_time.elapsed().as_secs_f64();
        METRICS.observe_request_duration(duration);
        METRICS.decrement_connections();

        result
    }

    async fn handle_connection_inner(
        &self,
        client_socket: &mut TcpStream,
        client_addr: SocketAddr,
        router: Arc<SniRouter>,
        dnssec_verifier: Arc<DnssecVerifier>,
        ech_decryptor: Option<Arc<EchDecryptor>>,
        ech_failure_logger: Option<Arc<EchFailureLogger>>,
        fingerprint_manager: Option<Arc<FingerprintManager>>,
        config: Arc<Config>,
    ) -> Result<(), ProxyError> {
        let mut buffer = BytesMut::with_capacity(config.buffer_size);
        buffer.resize(config.buffer_size, 0);

        let read_timeout = Duration::from_secs(config.timeout_secs);
        let n = timeout(read_timeout, client_socket.read(&mut buffer))
            .await
            .map_err(|_| ProxyError::Timeout)??;

        if n == 0 {
            return Ok(());
        }

        buffer.truncate(n);

        let client_hello = match TlsParser::parse_client_hello(&buffer) {
            Ok(info) => {
                METRICS.increment_sni_success();
                info
            }
            Err(e) => {
                METRICS.increment_sni_failure();
                debug!("Failed to parse ClientHello from {}: {}", client_addr, e);
                return Err(ProxyError::TlsParseError(e));
            }
        };

        let mut sni = client_hello.sni.as_deref();
        let mut ech_data = client_hello.ech.or(client_hello.esni);
        let mut used_ech = false;

        if let (Some(ech), Some(decryptor)) = (ech_data.as_ref(), ech_decryptor.as_ref()) {
            METRICS.increment_ech_attempt();
            let decrypt_start = Instant::now();

            match decryptor.decrypt_ech(ech) {
                Ok(ech_result) => {
                    METRICS.increment_ech_success();
                    let decrypt_duration = decrypt_start.elapsed().as_secs_f64();
                    METRICS.observe_ech_decryption_time(decrypt_duration);

                    if let Some(inner_sni) = ech_result.sni.as_deref() {
                        sni = Some(inner_sni);
                        used_ech = true;
                        info!("ECH decrypted successfully - Inner SNI: {}", inner_sni);
                    }
                }
                Err(e) => {
                    METRICS.increment_ech_failure(&format!("{:?}", e));
                    warn!("ECH decryption failed for client {}: {}", client_addr, e);

                    if let Some(logger) = ech_failure_logger.as_ref() {
                        logger.log_failure(&client_addr, &e, ech);
                    }

                    if config.ech_fallback_enabled {
                        METRICS.increment_ech_fallback();
                        info!("Falling back to public SNI for client {}", client_addr);
                    } else {
                        return Err(ProxyError::EchDecryptionFailed);
                    }
                }
            }
        }

        let mut identified_app: Option<String> = None;
        let mut fingerprint_confidence: Option<f32> = None;

        if sni.is_none() || used_ech {
            if let Some(ref fp_manager) = fingerprint_manager {
                if let Some(result) = fp_manager.identify(&buffer, &client_addr.to_string()) {
                    identified_app = Some(result.app_name.clone());
                    fingerprint_confidence = Some(result.confidence);
                    info!(
                        "TLS fingerprint identified: {} (confidence: {:.2})",
                        result.app_name, result.confidence
                    );
                }
            }
        }

        info!(
            "Connection from {} - SNI: {:?} (ECH: {}, Fingerprint: {:?})",
            client_addr, sni, used_ech, identified_app
        );

        if let Some(domain) = sni {
            METRICS.increment_requests(domain);

            if config.dnssec_verify {
                METRICS.increment_dnssec_verification();
                match dnssec_verifier.verify_domain(domain).await {
                    Ok(true) => {}
                    Ok(false) => {
                        METRICS.increment_dnssec_failure();
                        warn!("DNSSEC verification failed for domain: {}", domain);
                        return Err(ProxyError::DnssecFailed);
                    }
                    Err(e) => {
                        METRICS.increment_dnssec_failure();
                        warn!("DNSSEC verification error for {}: {}", domain, e);
                    }
                }
            }
        }

        if let Some(ref app) = identified_app {
            METRICS.increment_requests(app);
        }

        let decision = router.make_decision(sni);
        match decision {
            RoutingDecision::Block => {
                if let Some(domain) = sni {
                    METRICS.increment_blocked(domain);
                    info!("Blocked connection to domain: {}", domain);
                }
                return Err(ProxyError::Blocked);
            }
            RoutingDecision::Allow | RoutingDecision::Passthrough => {}
        }

        let target_addr = if let Some(domain) = sni {
            self.resolve_target(Some(domain), client_hello.clone()).await?
        } else if let Some(ref app) = identified_app {
            self.resolve_target(Some(app), client_hello.clone()).await?
        } else {
            return Err(ProxyError::ConnectionError(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "No SNI or fingerprint available to resolve",
            )));
        };

        METRICS.increment_backend_connection(&target_addr.to_string());

        let mut backend_socket = timeout(
            read_timeout,
            TcpStream::connect(target_addr),
        )
        .await
        .map_err(|_| ProxyError::Timeout)??;

        backend_socket.write_all(&buffer).await?;

        let (mut client_read, mut client_write) = client_socket.split();
        let (mut backend_read, mut backend_write) = backend_socket.split();

        let client_to_backend = async {
            let mut buf = vec![0u8; config.buffer_size];
            loop {
                match client_read.read(&mut buf).await {
                    Ok(0) => break Ok(()),
                    Ok(n) => {
                        METRICS.add_bytes_transferred(n as u64);
                        backend_write.write_all(&buf[..n]).await?;
                    }
                    Err(e) => break Err(e),
                }
            }
        };

        let backend_to_client = async {
            let mut buf = vec![0u8; config.buffer_size];
            loop {
                match backend_read.read(&mut buf).await {
                    Ok(0) => break Ok(()),
                    Ok(n) => {
                        METRICS.add_bytes_transferred(n as u64);
                        client_write.write_all(&buf[..n]).await?;
                    }
                    Err(e) => break Err(e),
                }
            }
        };

        tokio::select! {
            result = client_to_backend => {
                if let Err(e) = result {
                    debug!("Client to backend error: {}", e);
                }
            }
            result = backend_to_client => {
                if let Err(e) = result {
                    debug!("Backend to client error: {}", e);
                }
            }
        }

        Ok(())
    }

    async fn resolve_target(
        &self,
        sni: Option<&str>,
        _client_hello: ClientHelloInfo,
    ) -> Result<SocketAddr, ProxyError> {
        match sni {
            Some(domain) => {
                let port = 443u16;

                let addrs = tokio::net::lookup_host((domain, port))
                    .await
                    .map_err(|e| ProxyError::ConnectionError(e))?;

                addrs
                    .next()
                    .ok_or_else(|| {
                        ProxyError::ConnectionError(std::io::Error::new(
                            std::io::ErrorKind::NotFound,
                            "No address found",
                        ))
                    })
            }
            None => Err(ProxyError::ConnectionError(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "No SNI to resolve",
            ))),
        }
    }
}

impl Clone for Proxy {
    fn clone(&self) -> Self {
        Self {
            config: Arc::clone(&self.config),
            router: Arc::clone(&self.router),
            dnssec_verifier: Arc::clone(&self.dnssec_verifier),
            ech_decryptor: self.ech_decryptor.clone(),
            ech_failure_logger: self.ech_failure_logger.clone(),
            fingerprint_manager: self.fingerprint_manager.clone(),
        }
    }
}
