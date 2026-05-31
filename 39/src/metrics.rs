use std::sync::Arc;

use prometheus::{
    register_histogram_vec_with_registry, register_int_counter_vec_with_registry,
    register_int_gauge_vec_with_registry, Encoder, HistogramVec, IntCounterVec, IntGaugeVec,
    Registry, TextEncoder,
};

use crate::compression::Algo;

#[derive(Clone)]
pub struct Metrics {
    inner: Arc<Inner>,
}

struct Inner {
    registry: Registry,
    requests_total: IntCounterVec,
    cache_hits_total: IntCounterVec,
    origin_fetch_total: IntCounterVec,
    compression_duration_seconds: HistogramVec,
    cache_bytes: IntGaugeVec,
    warmup_total: IntCounterVec,
    compression_bytes_total: IntCounterVec,
    compression_count_total: IntCounterVec,
}

impl Metrics {
    pub fn new() -> Self {
        let registry = Registry::new();

        let requests_total = register_int_counter_vec_with_registry!(
            "cdn_requests_total",
            "Total number of requests served",
            &["method", "status"],
            registry,
        )
        .unwrap();

        let cache_hits_total = register_int_counter_vec_with_registry!(
            "cdn_cache_hits_total",
            "Total number of cache hits and misses",
            &["result"],
            registry,
        )
        .unwrap();

        let origin_fetch_total = register_int_counter_vec_with_registry!(
            "cdn_origin_fetch_total",
            "Total number of origin fetches",
            &["status"],
            registry,
        )
        .unwrap();

        let compression_duration_seconds = register_histogram_vec_with_registry!(
            "cdn_compression_duration_seconds",
            "Compression time distribution",
            &["algo", "tier"],
            vec![0.001, 0.005, 0.010, 0.025, 0.050, 0.100, 0.250, 0.500, 1.0, 2.5, 5.0],
            registry,
        )
        .unwrap();

        let cache_bytes = register_int_gauge_vec_with_registry!(
            "cdn_cache_bytes",
            "Total bytes currently stored in on-disk cache",
            &["encoding"],
            registry,
        )
        .unwrap();

        let warmup_total = register_int_counter_vec_with_registry!(
            "cdn_warmup_total",
            "Total number of cache warmup tasks",
            &["status"],
            registry,
        )
        .unwrap();

        let compression_bytes_total = register_int_counter_vec_with_registry!(
            "cdn_compression_bytes_total",
            "Total bytes processed by compression",
            &["algo", "tier", "direction"],
            registry,
        )
        .unwrap();

        let compression_count_total = register_int_counter_vec_with_registry!(
            "cdn_compression_count_total",
            "Total number of compression operations",
            &["algo", "tier"],
            registry,
        )
        .unwrap();

        Self {
            inner: Arc::new(Inner {
                registry,
                requests_total,
                cache_hits_total,
                origin_fetch_total,
                compression_duration_seconds,
                cache_bytes,
                warmup_total,
                compression_bytes_total,
                compression_count_total,
            }),
        }
    }

    pub fn inc_request(&self, method: &str, status: u16) {
        self.inner
            .requests_total
            .with_label_values(&[method, &status.to_string()])
            .inc();
    }

    pub fn inc_cache_hit(&self) {
        self.inner
            .cache_hits_total
            .with_label_values(&["hit"])
            .inc();
    }

    pub fn inc_cache_miss(&self) {
        self.inner
            .cache_hits_total
            .with_label_values(&["miss"])
            .inc();
    }

    pub fn inc_origin_fetch(&self, status: u16) {
        self.inner
            .origin_fetch_total
            .with_label_values(&[&status.to_string()])
            .inc();
    }

    pub fn observe_compression(&self, algo: Algo, tier: &str, dur_secs: f64) {
        self.inner
            .compression_duration_seconds
            .with_label_values(&[algo.content_encoding(), tier])
            .observe(dur_secs);
    }

    pub fn set_cache_bytes(&self, encoding: &str, bytes: u64) {
        self.inner
            .cache_bytes
            .with_label_values(&[encoding])
            .set(bytes as i64);
    }

    pub fn inc_warmup(&self, status: &str) {
        self.inner
            .warmup_total
            .with_label_values(&[status])
            .inc();
    }

    pub fn record_compression(
        &self,
        algo: Algo,
        tier: &str,
        original_size: u64,
        compressed_size: u64,
    ) {
        let enc = algo.content_encoding();
        self.inner
            .compression_bytes_total
            .with_label_values(&[enc, tier, "original"])
            .inc_by(original_size);
        self.inner
            .compression_bytes_total
            .with_label_values(&[enc, tier, "compressed"])
            .inc_by(compressed_size);
        self.inner
            .compression_count_total
            .with_label_values(&[enc, tier])
            .inc();
    }

    pub fn encode(&self) -> Vec<u8> {
        let encoder = TextEncoder::new();
        let mut buf = Vec::new();
        let metric_families = self.inner.registry.gather();
        let _ = encoder.encode(&metric_families, &mut buf);
        buf
    }
}
