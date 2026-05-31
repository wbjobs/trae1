use crate::stats::Stats;
use axum::{routing::get, Router};
use prometheus::{register_counter_vec, register_gauge_vec, CounterVec, Encoder, GaugeVec, Registry, TextEncoder};
use std::net::SocketAddr;
use std::sync::OnceLock;

static REGISTRY: OnceLock<Registry> = OnceLock::new();
static UPSTREAM_REQUESTS: OnceLock<CounterVec> = OnceLock::new();
static UPSTREAM_ERRORS: OnceLock<CounterVec> = OnceLock::new();
static UPSTREAM_LATENCY: OnceLock<GaugeVec> = OnceLock::new();
static CACHE_HITS: OnceLock<CounterVec> = OnceLock::new();
static CACHE_MISSES: OnceLock<CounterVec> = OnceLock::new();

fn get_registry() -> &'static Registry {
    REGISTRY.get_or_init(|| {
        let registry = Registry::new();

        let requests = register_counter_vec!(
            "dns_requests_total",
            "Total DNS requests by upstream and protocol",
            &["upstream", "protocol"]
        )
        .unwrap();
        registry.register(Box::new(requests.clone())).unwrap();
        UPSTREAM_REQUESTS.set(requests).unwrap();

        let errors = register_counter_vec!(
            "dns_request_errors_total",
            "Total DNS request errors by upstream and protocol",
            &["upstream", "protocol"]
        )
        .unwrap();
        registry.register(Box::new(errors.clone())).unwrap();
        UPSTREAM_ERRORS.set(errors).unwrap();

        let latency = register_gauge_vec!(
            "dns_request_duration_seconds",
            "Last DNS request duration by upstream and protocol",
            &["upstream", "protocol"]
        )
        .unwrap();
        registry.register(Box::new(latency.clone())).unwrap();
        UPSTREAM_LATENCY.set(latency).unwrap();

        let cache_hits = register_counter_vec!(
            "dns_cache_hits_total",
            "Total cache hits",
            &["type"]
        )
        .unwrap();
        registry.register(Box::new(cache_hits.clone())).unwrap();
        CACHE_HITS.set(cache_hits).unwrap();

        let cache_misses = register_counter_vec!(
            "dns_cache_misses_total",
            "Total cache misses",
            &["type"]
        )
        .unwrap();
        registry.register(Box::new(cache_misses.clone())).unwrap();
        CACHE_MISSES.set(cache_misses).unwrap();

        registry
    })
}

#[allow(dead_code)]
pub fn record_request(upstream: &str, protocol: &str) {
    if let Some(c) = UPSTREAM_REQUESTS.get() {
        c.with_label_values(&[upstream, protocol]).inc();
    }
}

#[allow(dead_code)]
pub fn record_error(upstream: &str, protocol: &str) {
    if let Some(c) = UPSTREAM_ERRORS.get() {
        c.with_label_values(&[upstream, protocol]).inc();
    }
}

#[allow(dead_code)]
pub fn record_latency(upstream: &str, protocol: &str, seconds: f64) {
    if let Some(g) = UPSTREAM_LATENCY.get() {
        g.with_label_values(&[upstream, protocol]).set(seconds);
    }
}

pub fn record_cache_hit() {
    if let Some(c) = CACHE_HITS.get() {
        c.with_label_values(&["dns"]).inc();
    }
}

pub fn record_cache_miss() {
    if let Some(c) = CACHE_MISSES.get() {
        c.with_label_values(&["dns"]).inc();
    }
}

pub async fn serve_metrics_endpoint(listen: SocketAddr) -> anyhow::Result<()> {
    get_registry();

    let app = Router::new().route(
        "/metrics",
        get(|| async {
            let registry = get_registry();
            let encoder = TextEncoder::new();
            let metric_families = registry.gather();
            let mut buffer = vec![];
            encoder.encode(&metric_families, &mut buffer).unwrap();
            String::from_utf8(buffer).unwrap()
        }),
    );

    tracing::info!(address = %listen, "Prometheus metrics 端点启动");
    let listener = tokio::net::TcpListener::bind(listen).await?;
    axum::serve(listener, app).await?;
    Ok(())
}

#[allow(dead_code)]
pub fn sync_stats_to_prometheus(stats: &Stats) {
    let snap = stats.snapshot();
    for u in &snap.upstreams {
        record_latency(&u.name, "unknown", u.last_latency_ms / 1000.0);
    }
}