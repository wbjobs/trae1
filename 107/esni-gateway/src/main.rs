mod config;
mod connection_pool;
mod cnn_model;
mod dnssec;
mod ech;
mod fingerprint;
mod fingerprint_manager;
mod metrics;
mod online_learning;
mod proxy;
mod router;
mod tls_parser;
mod worker_pool;

use crate::config::Config;
use crate::dnssec::DnssecVerifier;
use crate::ech::{EchDecryptor, EchFailureLogger};
use crate::fingerprint_manager::FingerprintManager;
use crate::metrics::{metrics_handler, METRICS};
use crate::proxy::Proxy;
use crate::router::SniRouter;
use clap::Parser;
use std::sync::Arc;
use tokio::net::TcpListener;
use tracing::{error, info};
use tracing_subscriber::EnvFilter;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let config = Config::parse();

    if let Err(e) = config.validate() {
        eprintln!("Configuration error: {}", e);
        std::process::exit(1);
    }

    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new(&config.log_level)),
        )
        .init();

    info!("Starting ESNI Gateway");
    info!("Configuration: {:?}", config);

    let router = Arc::new(SniRouter::new(config.whitelist_mode));

    if let Some(ref block_list_path) = config.block_list {
        match router.load_block_list(block_list_path) {
            Ok(count) => info!("Loaded {} domains into block list", count),
            Err(e) => {
                error!("Failed to load block list: {}", e);
                std::process::exit(1);
            }
        }
    }

    if let Some(ref allow_list_path) = config.allow_list {
        match router.load_allow_list(allow_list_path) {
            Ok(count) => info!("Loaded {} domains into allow list", count),
            Err(e) => {
                error!("Failed to load allow list: {}", e);
                std::process::exit(1);
            }
        }
    }

    let dnssec_verifier = Arc::new(DnssecVerifier::new(config.dnssec_verify)?);
    info!("DNSSEC verification: {}", config.dnssec_verify);

    let ech_decryptor = if let Some(ref ech_key_path) = config.ech_key {
        match EchDecryptor::from_file(ech_key_path) {
            Ok(decryptor) => {
                info!("ECH decryption enabled: loaded {} keys", decryptor.get_key_count());
                Some(Arc::new(decryptor))
            }
            Err(e) => {
                error!("Failed to load ECH keys: {}", e);
                std::process::exit(1);
            }
        }
    } else {
        info!("ECH decryption disabled: no key file specified");
        None
    };

    let ech_failure_logger = if let Some(ref log_path) = config.ech_failure_log {
        match EchFailureLogger::new(Some(log_path)) {
            Ok(logger) => {
                info!("ECH failure logging enabled: {:?}", log_path);
                Some(Arc::new(logger))
            }
            Err(e) => {
                error!("Failed to create ECH failure logger: {}", e);
                None
            }
        }
    } else {
        info!("ECH failure logging disabled");
        None
    };

    let fingerprint_manager = if config.enable_fingerprint {
        let mut manager = FingerprintManager::new();
        manager.enable();
        manager.set_confidence_threshold(config.fingerprint_threshold);
        manager.set_use_cache(config.fingerprint_use_cache);

        if let Some(ref model_path) = config.fingerprint_db {
            match manager.load_model(model_path) {
                Ok(_) => info!("Loaded fingerprint model from {:?}", model_path),
                Err(e) => {
                    error!("Failed to load fingerprint model: {}", e);
                    std::process::exit(1);
                }
            }
        }

        if let Some(ref db_path) = config.app_database {
            match manager.load_app_database(db_path) {
                Ok(_) => info!("Loaded application database from {:?}", db_path),
                Err(e) => {
                    error!("Failed to load application database: {}", e);
                    std::process::exit(1);
                }
            }
        }

        info!("TLS fingerprint recognition enabled");
        Some(Arc::new(manager))
    } else {
        info!("TLS fingerprint recognition disabled");
        None
    };

    let metrics_addr = config.metrics_addr;
    tokio::spawn(async move {
        if let Err(e) = start_metrics_server(metrics_addr).await {
            error!("Metrics server error: {}", e);
        }
    });

    let workers = config.workers;
    info!("Starting {} worker threads", workers);

    let proxy = Proxy::new(
        Arc::new(config),
        Arc::clone(&router),
        Arc::clone(&dnssec_verifier),
        ech_decryptor,
        ech_failure_logger,
        fingerprint_manager,
    );

    if let Err(e) = proxy.run().await {
        error!("Proxy error: {}", e);
        return Err(e.into());
    }

    Ok(())
}

async fn start_metrics_server(addr: std::net::SocketAddr) -> Result<(), Box<dyn std::error::Error>> {
    let listener = TcpListener::bind(addr).await?;
    info!("Metrics server listening on {}", addr);

    loop {
        let (mut socket, _) = listener.accept().await?;

        tokio::spawn(async move {
            use tokio::io::AsyncWriteExt;

            let metrics = metrics_handler();
            let response = format!(
                "HTTP/1.1 200 OK\r\nContent-Type: text/plain; version=0.0.4\r\nContent-Length: {}\r\n\r\n{}",
                metrics.len(),
                metrics
            );

            let _ = socket.write_all(response.as_bytes()).await;
            let _ = socket.flush().await;
        });
    }
}
