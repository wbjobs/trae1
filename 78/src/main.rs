mod cache;
mod config;
mod dnscrypt;
mod health_check;
mod metrics_api;
mod prometheus_metrics;
mod server;
mod stats;
mod transport;

use anyhow::Result;
use clap::Parser;
use std::net::SocketAddr;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;

use crate::cache::Cache;
use crate::config::Config;
use crate::health_check::run_health_checks;
use crate::server::{run_cache_cleaner, DnsHandler, start_dns_server};
use crate::stats::Stats;
use crate::transport::{build_transports, UpstreamPool};

#[derive(Parser, Debug)]
#[command(version, about, long_about = None)]
struct Cli {
    #[arg(short, long, default_value = "config.toml")]
    config: PathBuf,

    #[arg(short, long)]
    listen: Option<String>,

    #[arg(short, long)]
    metrics_listen: Option<String>,
}

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .with_target(false)
        .init();

    let cli = Cli::parse();
    let mut cfg = Config::load(&cli.config)?;

    if let Some(listen) = cli.listen {
        cfg.listen = listen;
    }
    if let Some(metrics_listen) = cli.metrics_listen {
        cfg.metrics_listen = metrics_listen;
    }

    if cfg.upstreams.is_empty() {
        anyhow::bail!("未配置任何上游 DNSCrypt 服务器");
    }

    let stats = Stats::new();
    stats.set_active(&cfg.upstreams[0].name);

    let cache = Arc::new(Cache::new(
        cfg.cache_max_entries,
        cfg.prefetch_ttl_ratio,
        stats.clone(),
    ));

    let transports = build_transports(
        &cfg.upstreams,
        stats.clone(),
        cfg.edns0_max_buffer_size as u16,
        cfg.tcp_max_retries,
        cfg.tcp_pool_max_size,
        cfg.health_check_interval_secs,
    )
    .await?;
    let pool = Arc::new(UpstreamPool::new(transports, stats.clone()));

    let handler = Arc::new(DnsHandler::new(
        cache.clone(),
        pool.clone(),
        stats.clone(),
        cfg.prefetch_max_parallel,
    ));

    let listen_addr: SocketAddr = cfg.listen.parse()?;
    let metrics_addr: SocketAddr = cfg.metrics_listen.parse()?;

    let cleaner_cache = cache.clone();
    tokio::spawn(async move {
        run_cache_cleaner(cleaner_cache, Duration::from_secs(10)).await;
    });

    let metrics_stats = stats.clone();
    tokio::spawn(async move {
        if let Err(e) = metrics_api::serve_metrics(metrics_addr, metrics_stats).await {
            tracing::error!(error = %e, "metrics API 退出");
        }
    });

    let health_pool = pool.clone();
    let hc_interval = cfg.health_check_interval_secs;
    tokio::spawn(async move {
        run_health_checks(health_pool, hc_interval).await;
    });

    let prom_addr: SocketAddr = format!("0.0.0.0:{}", metrics_addr.port() + 1).parse()?;
    tokio::spawn(async move {
        if let Err(e) = prometheus_metrics::serve_metrics_endpoint(prom_addr).await {
            tracing::error!(error = %e, "Prometheus metrics 退出");
        }
    });

    tracing::info!(
        "DNSCrypt 递归解析器启动: listen={}, metrics={}, upstreams={}, cache_max={}",
        listen_addr,
        metrics_addr,
        cfg.upstreams.len(),
        cfg.cache_max_entries
    );

    start_dns_server(listen_addr, handler).await?;
    Ok(())
}
