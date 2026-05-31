use lazy_static::lazy_static;
use prometheus::{
    Counter, CounterVec, Gauge, Histogram, HistogramOpts, IntCounter, IntCounterVec, IntGauge,
    Opts, Registry,
};
use std::sync::OnceLock;

lazy_static! {
    pub static ref METRICS: Metrics = Metrics::new();
}

pub struct Metrics {
    pub registry: Registry,
    pub connections_total: IntCounter,
    pub connections_active: IntGauge,
    pub requests_total: IntCounterVec,
    pub requests_blocked: IntCounterVec,
    pub request_duration: Histogram,
    pub sni_parse_success: IntCounter,
    pub sni_parse_failure: IntCounter,
    pub dnssec_verifications: IntCounter,
    pub dnssec_failures: IntCounter,
    pub bytes_transferred: IntCounter,
    pub backend_connections: IntCounterVec,
    pub ech_attempts: IntCounter,
    pub ech_success: IntCounter,
    pub ech_failure: IntCounterVec,
    pub ech_decryption_time: Histogram,
    pub ech_fallback_total: IntCounter,
    pub fingerprint_extractions: IntCounter,
    pub fingerprint_predictions: IntCounterVec,
    pub fingerprint_cache_hits: IntCounter,
    pub fingerprint_cache_misses: IntCounter,
    pub fingerprint_extraction_failures: IntCounter,
    pub fingerprint_low_confidence: IntCounter,
    pub fingerprint_confidence: Histogram,
}

impl Metrics {
    fn new() -> Self {
        let registry = Registry::new();

        let connections_total = IntCounter::new(
            "esni_connections_total",
            "Total number of connections handled",
        )
        .unwrap();
        registry.register(Box::new(connections_total.clone())).unwrap();

        let connections_active = IntGauge::new(
            "esni_connections_active",
            "Number of active connections",
        )
        .unwrap();
        registry.register(Box::new(connections_active.clone())).unwrap();

        let requests_total = IntCounterVec::new(
            Opts::new("esni_requests_total", "Total requests by SNI domain"),
            &["domain"],
        )
        .unwrap();
        registry
            .register(Box::new(requests_total.clone()))
            .unwrap();

        let requests_blocked = IntCounterVec::new(
            Opts::new("esni_requests_blocked", "Blocked requests by SNI domain"),
            &["domain"],
        )
        .unwrap();
        registry
            .register(Box::new(requests_blocked.clone()))
            .unwrap();

        let request_duration = Histogram::with_opts(HistogramOpts::new(
            "esni_request_duration_seconds",
            "Request duration in seconds",
        ))
        .unwrap();
        registry
            .register(Box::new(request_duration.clone()))
            .unwrap();

        let sni_parse_success = IntCounter::new(
            "esni_sni_parse_success",
            "Number of successful SNI parses",
        )
        .unwrap();
        registry
            .register(Box::new(sni_parse_success.clone()))
            .unwrap();

        let sni_parse_failure = IntCounter::new(
            "esni_sni_parse_failure",
            "Number of failed SNI parses",
        )
        .unwrap();
        registry
            .register(Box::new(sni_parse_failure.clone()))
            .unwrap();

        let dnssec_verifications = IntCounter::new(
            "esni_dnssec_verifications",
            "Number of DNSSEC verifications performed",
        )
        .unwrap();
        registry
            .register(Box::new(dnssec_verifications.clone()))
            .unwrap();

        let dnssec_failures = IntCounter::new(
            "esni_dnssec_failures",
            "Number of DNSSEC verification failures",
        )
        .unwrap();
        registry
            .register(Box::new(dnssec_failures.clone()))
            .unwrap();

        let bytes_transferred = IntCounter::new(
            "esni_bytes_transferred",
            "Total bytes transferred",
        )
        .unwrap();
        registry
            .register(Box::new(bytes_transferred.clone()))
            .unwrap();

        let backend_connections = IntCounterVec::new(
            Opts::new("esni_backend_connections", "Backend connections by target"),
            &["backend"],
        )
        .unwrap();
        registry
            .register(Box::new(backend_connections.clone()))
            .unwrap();

        let ech_attempts = IntCounter::new(
            "ech_attempts",
            "Total number of ECH decryption attempts",
        )
        .unwrap();
        registry.register(Box::new(ech_attempts.clone())).unwrap();

        let ech_success = IntCounter::new(
            "ech_success",
            "Number of successful ECH decryptions",
        )
        .unwrap();
        registry.register(Box::new(ech_success.clone())).unwrap();

        let ech_failure = IntCounterVec::new(
            Opts::new("ech_failure", "ECH decryption failures by error type"),
            &["error_type"],
        )
        .unwrap();
        registry.register(Box::new(ech_failure.clone())).unwrap();

        let ech_decryption_time = Histogram::with_opts(HistogramOpts::new(
            "ech_decryption_time_seconds",
            "Time taken to decrypt ECH",
        ))
        .unwrap();
        registry.register(Box::new(ech_decryption_time.clone())).unwrap();

        let ech_fallback_total = IntCounter::new(
            "ech_fallback_total",
            "Number of times ECH fallback was triggered",
        )
        .unwrap();
        registry.register(Box::new(ech_fallback_total.clone())).unwrap();

        let fingerprint_extractions = IntCounter::new(
            "fingerprint_extractions_total",
            "Total number of TLS fingerprint extractions",
        )
        .unwrap();
        registry
            .register(Box::new(fingerprint_extractions.clone()))
            .unwrap();

        let fingerprint_predictions = IntCounterVec::new(
            Opts::new("fingerprint_predictions_total", "Fingerprint predictions by application"),
            &["app"],
        )
        .unwrap();
        registry
            .register(Box::new(fingerprint_predictions.clone()))
            .unwrap();

        let fingerprint_cache_hits = IntCounter::new(
            "fingerprint_cache_hits_total",
            "Number of fingerprint cache hits",
        )
        .unwrap();
        registry
            .register(Box::new(fingerprint_cache_hits.clone()))
            .unwrap();

        let fingerprint_cache_misses = IntCounter::new(
            "fingerprint_cache_misses_total",
            "Number of fingerprint cache misses",
        )
        .unwrap();
        registry
            .register(Box::new(fingerprint_cache_misses.clone()))
            .unwrap();

        let fingerprint_extraction_failures = IntCounter::new(
            "fingerprint_extraction_failures_total",
            "Number of fingerprint extraction failures",
        )
        .unwrap();
        registry
            .register(Box::new(fingerprint_extraction_failures.clone()))
            .unwrap();

        let fingerprint_low_confidence = IntCounter::new(
            "fingerprint_low_confidence_total",
            "Number of low confidence fingerprint predictions",
        )
        .unwrap();
        registry
            .register(Box::new(fingerprint_low_confidence.clone()))
            .unwrap();

        let fingerprint_confidence = Histogram::with_opts(HistogramOpts::new(
            "fingerprint_confidence",
            "Confidence of fingerprint predictions",
        ))
        .unwrap();
        registry
            .register(Box::new(fingerprint_confidence.clone()))
            .unwrap();

        Self {
            registry,
            connections_total,
            connections_active,
            requests_total,
            requests_blocked,
            request_duration,
            sni_parse_success,
            sni_parse_failure,
            dnssec_verifications,
            dnssec_failures,
            bytes_transferred,
            backend_connections,
            ech_attempts,
            ech_success,
            ech_failure,
            ech_decryption_time,
            ech_fallback_total,
            fingerprint_extractions,
            fingerprint_predictions,
            fingerprint_cache_hits,
            fingerprint_cache_misses,
            fingerprint_extraction_failures,
            fingerprint_low_confidence,
            fingerprint_confidence,
        }
    }

    pub fn increment_connections(&self) {
        self.connections_total.inc();
        self.connections_active.inc();
    }

    pub fn decrement_connections(&self) {
        self.connections_active.dec();
    }

    pub fn increment_requests(&self, domain: &str) {
        self.requests_total.with_label_values(&[domain]).inc();
    }

    pub fn increment_blocked(&self, domain: &str) {
        self.requests_blocked.with_label_values(&[domain]).inc();
    }

    pub fn observe_request_duration(&self, duration: f64) {
        self.request_duration.observe(duration);
    }

    pub fn increment_sni_success(&self) {
        self.sni_parse_success.inc();
    }

    pub fn increment_sni_failure(&self) {
        self.sni_parse_failure.inc();
    }

    pub fn increment_dnssec_verification(&self) {
        self.dnssec_verifications.inc();
    }

    pub fn increment_dnssec_failure(&self) {
        self.dnssec_failures.inc();
    }

    pub fn add_bytes_transferred(&self, bytes: u64) {
        self.bytes_transferred.inc_by(bytes);
    }

    pub fn increment_backend_connection(&self, backend: &str) {
        self.backend_connections.with_label_values(&[backend]).inc();
    }

    pub fn increment_ech_attempt(&self) {
        self.ech_attempts.inc();
    }

    pub fn increment_ech_success(&self) {
        self.ech_success.inc();
    }

    pub fn increment_ech_failure(&self, error_type: &str) {
        self.ech_failure.with_label_values(&[error_type]).inc();
    }

    pub fn observe_ech_decryption_time(&self, duration: f64) {
        self.ech_decryption_time.observe(duration);
    }

    pub fn increment_ech_fallback(&self) {
        self.ech_fallback_total.inc();
    }

    pub fn increment_fingerprint_extraction(&self) {
        self.fingerprint_extractions.inc();
    }

    pub fn increment_fingerprint_prediction(&self, app_name: &str) {
        self.fingerprint_predictions
            .with_label_values(&[app_name])
            .inc();
    }

    pub fn increment_fingerprint_cache_hit(&self) {
        self.fingerprint_cache_hits.inc();
    }

    pub fn increment_fingerprint_cache_miss(&self) {
        self.fingerprint_cache_misses.inc();
    }

    pub fn increment_fingerprint_extraction_failure(&self) {
        self.fingerprint_extraction_failures.inc();
    }

    pub fn increment_fingerprint_low_confidence(&self) {
        self.fingerprint_low_confidence.inc();
    }

    pub fn observe_fingerprint_confidence(&self, confidence: f64) {
        self.fingerprint_confidence.observe(confidence);
    }

    pub fn gather(&self) -> Vec<prometheus::proto::MetricFamily> {
        self.registry.gather()
    }
}

pub fn metrics_handler() -> String {
    use prometheus::Encoder;
    let encoder = prometheus::TextEncoder::new();
    let metric_families = METRICS.gather();
    let mut buffer = Vec::new();
    encoder.encode(&metric_families, &mut buffer).unwrap();
    String::from_utf8(buffer).unwrap()
}
