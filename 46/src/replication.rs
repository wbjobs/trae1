use std::sync::Arc;

use serde::{Deserialize, Serialize};
use tokio::time::{interval, Duration};
use tracing::{info, warn};

use crate::pool::{current_generation, PoolResult, Role, SqlitePool};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReplicationStatus {
    pub role: &'static str,
    pub generation: u64,
    pub lag_seconds: u64,
    pub last_sync_unix_ms: Option<u64>,
    pub master_url: Option<String>,
}

pub struct ReplicationState {
    pub pool: Arc<SqlitePool>,
    pub last_sync: Arc<std::sync::Mutex<Option<std::time::Instant>>>,
    pub http: reqwest::Client,
}

impl ReplicationState {
    pub fn new(pool: Arc<SqlitePool>) -> Self {
        ReplicationState {
            pool,
            last_sync: Arc::new(std::sync::Mutex::new(None)),
            http: reqwest::Client::builder()
                .timeout(Duration::from_secs(30))
                .build()
                .expect("http client"),
        }
    }

    pub fn status(&self) -> ReplicationStatus {
        let role = self.pool.role();
        let generation = current_generation();
        let (lag, last_ms) = match *self.last_sync.lock().unwrap() {
            Some(t) => {
                let lag = t.elapsed().as_secs();
                let now_ms = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_millis() as u64;
                let last_ms = now_ms.saturating_sub(t.elapsed().as_millis() as u64);
                (lag, Some(last_ms))
            }
            None => (u64::MAX, None),
        };
        ReplicationStatus {
            role: role.as_str(),
            generation,
            lag_seconds: lag,
            last_sync_unix_ms: last_ms,
            master_url: self.pool.master_url.read().unwrap().clone(),
        }
    }

    pub fn mark_sync(&self) {
        *self.last_sync.lock().unwrap() = Some(std::time::Instant::now());
    }

    pub async fn pull_once(&self) -> PoolResult<Option<u64>> {
        let upstream = self
            .pool
            .master_url
            .read()
            .unwrap()
            .clone()
            .ok_or_else(|| crate::error::AppError::Pool("no upstream configured".into()))?;

        let url = format!("{}/repl/snapshot", upstream.trim_end_matches('/'));
        let resp = self
            .http
            .get(&url)
            .send()
            .await
            .map_err(|e| crate::error::AppError::Pool(format!("pull snapshot: {e}")))?;
        if !resp.status().is_success() {
            return Err(crate::error::AppError::Pool(format!(
                "pull snapshot status: {}",
                resp.status()
            )));
        }
        let bytes = resp
            .bytes()
            .await
            .map_err(|e| crate::error::AppError::Pool(format!("pull snapshot body: {e}")))?;

        if bytes.is_empty() {
            return Ok(None);
        }

        let mut iter = bytes.splitn(2, |&b| b == b'\n');
        let header = iter
            .next()
            .ok_or_else(|| crate::error::AppError::Pool("missing header".into()))?;
        let body = iter
            .next()
            .ok_or_else(|| crate::error::AppError::Pool("missing body".into()))?;

        let gen: u64 = std::str::from_utf8(header)
            .map_err(|e| crate::error::AppError::Pool(e.to_string()))?
            .trim()
            .parse()
            .map_err(|e| crate::error::AppError::Pool(format!("gen parse: {e}")))?;

        let pool = self.pool.clone();
        let body_owned = body.to_vec();
        tokio::task::spawn_blocking(move || pool.apply_snapshot_bytes(&body_owned))
            .await
            .map_err(|e| crate::error::AppError::Pool(e.to_string()))??;

        self.mark_sync();
        Ok(Some(gen))
    }
}

pub fn build_snapshot_body(gen: u64, bytes: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(64 + bytes.len());
    out.extend_from_slice(gen.to_string().as_bytes());
    out.push(b'\n');
    out.extend_from_slice(bytes);
    out
}

pub async fn run_pull_task(state: Arc<ReplicationState>) {
    let mut ticker = interval(Duration::from_secs(5));
    loop {
        ticker.tick().await;
        if state.pool.role() != Role::Slave {
            continue;
        }
        match state.pull_once().await {
            Ok(Some(gen)) => {
                info!(gen, "pulled snapshot from master");
            }
            Ok(None) => {}
            Err(e) => {
                warn!(error = %e, "pull failed");
            }
        }
    }
}

pub async fn promote_to_master(state: &ReplicationState) -> PoolResult<()> {
    state.pool.promote_to_master().await
}

pub async fn demote_to_slave(state: &ReplicationState, upstream: String) -> PoolResult<()> {
    state.pool.demote_to_slave().await?;
    *state.pool.master_url.write().unwrap() = Some(upstream);
    Ok(())
}

pub type ReplicationResult<T> = Result<T, crate::error::AppError>;
