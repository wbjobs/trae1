use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Instant;

use anyhow::{Context, Result};
use bytes::Bytes;
use lru::LruCache;
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

use crate::compression::{compress, Algo};
use crate::config::Config;
use crate::metrics::Metrics;
use crate::strategy::Strategy;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CachedMeta {
    pub url_path: String,
    pub algo: Algo,
    pub size: u64,
    pub content_type: Option<String>,
    pub etag: String,
    pub created_at_secs: u64,
}

#[derive(Clone)]
pub struct DiskCache {
    inner: Arc<Inner>,
}

struct Inner {
    dir: PathBuf,
    max_bytes: u64,
    lru: Mutex<LruState>,
    metrics: Metrics,
    strategy: Strategy,
}

struct LruState {
    order: LruCache<String, u64>,
    sizes: HashMap<String, u64>,
    total_bytes: u64,
}

impl LruState {
    fn new(cap: usize) -> Self {
        Self {
            order: LruCache::new(
                std::num::NonZeroUsize::new(cap.max(1)).unwrap(),
            ),
            sizes: HashMap::new(),
            total_bytes: 0,
        }
    }
}

#[derive(Clone, Serialize, Deserialize)]
struct MetaFile {
    url_path: String,
    algo: Algo,
    content_type: Option<String>,
    etag: String,
    created_at_secs: u64,
}

impl DiskCache {
    pub fn new(config: &Config, metrics: Metrics, strategy: Strategy) -> Result<Self> {
        fs::create_dir_all(&config.cache_dir).with_context(|| {
            format!("create cache dir: {}", config.cache_dir.display())
        })?;

        let mut state = LruState::new(config.lru_capacity);
        Self::rebuild_index(&config.cache_dir, &mut state)?;

        let inner = Arc::new(Inner {
            dir: config.cache_dir.clone(),
            max_bytes: config.cache_max_bytes,
            lru: Mutex::new(state),
            metrics,
            strategy,
        });

        Ok(Self { inner })
    }

    fn rebuild_index(dir: &Path, state: &mut LruState) -> Result<()> {
        let mut entries: Vec<(PathBuf, u64, u64)> = Vec::new();
        if let Ok(read) = fs::read_dir(dir) {
            for entry in read.flatten() {
                let p = entry.path();
                if !p.is_file() {
                    continue;
                }
                if p.extension().and_then(|s| s.to_str()) != Some("meta") {
                    continue;
                }
                let meta_path = p.clone();
                let data_path = meta_path.with_extension("");
                let size = match fs::metadata(&data_path) {
                    Ok(m) => m.len(),
                    Err(_) => continue,
                };
                let created = entry
                    .metadata()
                    .and_then(|m| m.created())
                    .unwrap_or_else(|_| std::time::SystemTime::now());
                let secs = created
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_secs();
                entries.push((data_path, size, secs));
            }
        }
        entries.sort_by_key(|(_, _, t)| *t);
        for (data_path, size, _) in entries {
            let key = data_path
                .file_name()
                .and_then(|s| s.to_str())
                .unwrap_or("")
                .to_string();
            state.order.put(key.clone(), size);
            state.sizes.insert(key, size);
            state.total_bytes += size;
        }
        Ok(())
    }

    fn key_for(url_path: &str, algo: Algo) -> String {
        let mut hasher = Sha256::new();
        hasher.update(url_path.as_bytes());
        hasher.update(b"|");
        hasher.update(algo.content_encoding().as_bytes());
        let hash = hex::encode(&hasher.finalize()[..16]);
        format!("{}_{}", algo.content_encoding(), hash)
    }

    fn paths(&self, key: &str) -> (PathBuf, PathBuf) {
        let data = self.inner.dir.join(key);
        let meta = self.inner.dir.join(format!("{}.meta", key));
        (data, meta)
    }

    pub fn get(&self, url_path: &str, algo: Algo) -> Option<CachedMeta> {
        let key = Self::key_for(url_path, algo);
        let meta_path = self.inner.dir.join(format!("{}.meta", key));
        let data_path = self.inner.dir.join(&key);

        let meta_bytes = fs::read(&meta_path).ok()?;
        let meta: MetaFile = serde_json::from_slice(&meta_bytes).ok()?;
        let size = fs::metadata(&data_path).ok()?.len();

        {
            let mut lru = self.inner.lru.lock();
            lru.order.put(key.clone(), size);
            lru.sizes.insert(key, size);
        }

        Some(CachedMeta {
            url_path: meta.url_path,
            algo: meta.algo,
            size,
            content_type: meta.content_type,
            etag: meta.etag,
            created_at_secs: meta.created_at_secs,
        })
    }

    pub fn get_bytes(&self, url_path: &str, algo: Algo) -> Option<Bytes> {
        let key = Self::key_for(url_path, algo);
        let data_path = self.inner.dir.join(&key);
        let bytes = fs::read(&data_path).ok()?;

        {
            let mut lru = self.inner.lru.lock();
            let size = bytes.len() as u64;
            lru.order.put(key.clone(), size);
            lru.sizes.insert(key, size);
        }

        Some(Bytes::from(bytes))
    }

    pub fn contains(&self, url_path: &str, algo: Algo) -> bool {
        let key = Self::key_for(url_path, algo);
        self.inner.dir.join(&key).exists()
    }

    pub fn put(
        &self,
        url_path: &str,
        algo: Algo,
        data: Bytes,
        content_type: Option<String>,
    ) -> Result<CachedMeta> {
        let key = Self::key_for(url_path, algo);
        let (data_path, meta_path) = self.paths(&key);

        let size = data.len() as u64;
        fs::write(&data_path, &data).with_context(|| {
            format!("write cache data: {}", data_path.display())
        })?;

        let etag = {
            let mut hasher = Sha256::new();
            hasher.update(&data);
            hex::encode(&hasher.finalize()[..8])
        };
        let created = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();

        let meta = MetaFile {
            url_path: url_path.to_string(),
            algo,
            content_type: content_type.clone(),
            etag: etag.clone(),
            created_at_secs: created,
        };
        let meta_bytes = serde_json::to_vec(&meta)?;
        fs::write(&meta_path, meta_bytes).with_context(|| {
            format!("write cache meta: {}", meta_path.display())
        })?;

        {
            let mut lru = self.inner.lru.lock();
            lru.order.put(key.clone(), size);
            lru.sizes.insert(key, size);
            lru.total_bytes += size;
            self.evict_locked(&mut lru);
            let total = lru.total_bytes;
            drop(lru);
            self.inner
                .metrics
                .set_cache_bytes(algo.content_encoding(), total);
        }

        Ok(CachedMeta {
            url_path: url_path.to_string(),
            algo,
            size,
            content_type,
            etag,
            created_at_secs: created,
        })
    }

    pub async fn put_compressed_async(
        &self,
        url_path: String,
        algo: Algo,
        uncompressed: Bytes,
        content_type: Option<String>,
    ) -> Result<CachedMeta> {
        let cache = self.clone();
        let metrics = self.inner.metrics.clone();
        let strategy = self.inner.strategy.clone();
        let original_size = uncompressed.len() as u64;

        let handle = tokio::task::spawn_blocking(move || {
            let start = Instant::now();
            let lv = strategy.levels_for_size(original_size);
            let compressed = compress(
                &uncompressed,
                algo,
                lv.gzip,
                lv.brotli,
                lv.deflate,
            )?;
            let dur = start.elapsed().as_secs_f64();
            metrics.observe_compression(algo, lv.tier.as_str(), dur);
            metrics.record_compression(algo, lv.tier.as_str(), original_size, compressed.len() as u64);
            cache.put(&url_path, algo, Bytes::from(compressed), content_type)
        });

        handle.await.context("compression task join")?
    }

    fn evict_locked(&self, lru: &mut LruState) {
        while lru.total_bytes > self.inner.max_bytes {
            let victim = match lru.order.pop_lru() {
                Some((k, _)) => k,
                None => break,
            };
            if let Some(sz) = lru.sizes.remove(&victim) {
                lru.total_bytes = lru.total_bytes.saturating_sub(sz);
            }
            let _ = fs::remove_file(self.inner.dir.join(&victim));
            let _ = fs::remove_file(self.inner.dir.join(format!("{}.meta", victim)));
        }
    }

    pub fn total_bytes(&self) -> u64 {
        self.inner.lru.lock().total_bytes
    }
}
