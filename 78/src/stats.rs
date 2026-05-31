use parking_lot::Mutex;
use serde::Serialize;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Instant;

#[derive(Debug, Clone, Serialize)]
pub struct CacheStats {
    pub hits: u64,
    pub misses: u64,
    pub inserts: u64,
    pub evictions: u64,
    pub entries: usize,
}

#[derive(Debug, Clone, Serialize)]
pub struct UpstreamLatency {
    pub name: String,
    pub requests: u64,
    pub errors: u64,
    pub last_latency_ms: f64,
    pub avg_latency_ms: f64,
    pub p95_latency_ms: f64,
    pub p99_latency_ms: f64,
}

#[derive(Debug, Clone, Serialize)]
pub struct StatsSnapshot {
    pub cache: CacheStats,
    pub upstreams: Vec<UpstreamLatency>,
    pub total_queries: u64,
    pub active_upstream: String,
    pub prefetch_count: u64,
}

#[derive(Debug, Default)]
pub struct StatsInner {
    pub hits: u64,
    pub misses: u64,
    pub inserts: u64,
    pub evictions: u64,
    pub entries: usize,
    pub total_queries: u64,
    pub active_upstream: String,
    pub prefetch_count: u64,
    pub upstreams: HashMap<String, UpstreamStatsInner>,
}

#[derive(Debug)]
pub struct UpstreamStatsInner {
    pub requests: u64,
    pub errors: u64,
    pub last_latency_ms: f64,
    pub latencies: Vec<f64>,
}

impl Default for UpstreamStatsInner {
    fn default() -> Self {
        Self {
            requests: 0,
            errors: 0,
            last_latency_ms: 0.0,
            latencies: Vec::with_capacity(256),
        }
    }
}

#[derive(Clone, Debug)]
pub struct Stats {
    pub inner: Arc<Mutex<StatsInner>>,
}

impl Stats {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(Mutex::new(StatsInner::default())),
        }
    }

    pub fn hit(&self) {
        self.inner.lock().hits += 1;
    }

    pub fn miss(&self) {
        self.inner.lock().misses += 1;
    }

    pub fn insert(&self) {
        self.inner.lock().inserts += 1;
    }

    pub fn evict(&self) {
        self.inner.lock().evictions += 1;
    }

    pub fn set_entries(&self, n: usize) {
        self.inner.lock().entries = n;
    }

    pub fn inc_queries(&self) {
        self.inner.lock().total_queries += 1;
    }

    pub fn set_active(&self, name: &str) {
        self.inner.lock().active_upstream = name.to_string();
    }

    pub fn inc_prefetch(&self) {
        self.inner.lock().prefetch_count += 1;
    }

    pub fn record_upstream(&self, name: &str, start: Instant, ok: bool) {
        let elapsed = start.elapsed().as_secs_f64() * 1000.0;
        let mut guard = self.inner.lock();
        let entry = guard
            .upstreams
            .entry(name.to_string())
            .or_insert_with(UpstreamStatsInner::default);
        entry.requests += 1;
        if !ok {
            entry.errors += 1;
        }
        entry.last_latency_ms = elapsed;
        entry.latencies.push(elapsed);
        if entry.latencies.len() > 1024 {
            let new: Vec<f64> = entry.latencies[512..].to_vec();
            entry.latencies = new;
        }
    }

    pub fn snapshot(&self) -> StatsSnapshot {
        let guard = self.inner.lock();
        let upstreams = guard
            .upstreams
            .iter()
            .map(|(name, u)| UpstreamLatency {
                name: name.clone(),
                requests: u.requests,
                errors: u.errors,
                last_latency_ms: u.last_latency_ms,
                avg_latency_ms: avg(&u.latencies),
                p95_latency_ms: percentile(&u.latencies, 0.95),
                p99_latency_ms: percentile(&u.latencies, 0.99),
            })
            .collect();
        StatsSnapshot {
            cache: CacheStats {
                hits: guard.hits,
                misses: guard.misses,
                inserts: guard.inserts,
                evictions: guard.evictions,
                entries: guard.entries,
            },
            upstreams,
            total_queries: guard.total_queries,
            active_upstream: guard.active_upstream.clone(),
            prefetch_count: guard.prefetch_count,
        }
    }
}

fn avg(v: &[f64]) -> f64 {
    if v.is_empty() {
        return 0.0;
    }
    v.iter().sum::<f64>() / v.len() as f64
}

fn percentile(v: &[f64], p: f64) -> f64 {
    if v.is_empty() {
        return 0.0;
    }
    let mut s: Vec<f64> = v.iter().copied().collect();
    s.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    let idx = ((s.len() as f64 - 1.0) * p).round() as usize;
    s[idx.min(s.len() - 1)]
}
