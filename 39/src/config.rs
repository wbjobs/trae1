use std::path::PathBuf;

use clap::Parser;
use serde::{Deserialize, Serialize};

#[derive(Debug, Parser, Clone, Serialize, Deserialize)]
#[command(author, version, about = "Static asset CDN edge node")]
pub struct Config {
    #[arg(long, default_value = "0.0.0.0:8080", env = "CDN_BIND")]
    pub bind: String,

    #[arg(long, env = "CDN_ORIGIN")]
    pub origin: String,

    #[arg(long, default_value = "./cache", env = "CDN_CACHE_DIR")]
    pub cache_dir: PathBuf,

    #[arg(long, default_value_t = 10 * 1024 * 1024 * 1024, env = "CDN_CACHE_MAX_BYTES")]
    pub cache_max_bytes: u64,

    #[arg(long, default_value_t = 10_000, env = "CDN_LRU_CAPACITY")]
    pub lru_capacity: usize,

    #[arg(long, default_value_t = 6, env = "CDN_GZIP_LEVEL")]
    pub gzip_level: u32,

    #[arg(long, default_value_t = 5, env = "CDN_BROTLI_LEVEL")]
    pub brotli_level: u32,

    #[arg(long, default_value_t = 6, env = "CDN_DEFLATE_LEVEL")]
    pub deflate_level: u32,

    #[arg(long, default_value_t = 1024, env = "CDN_WARMUP_CONCURRENCY")]
    pub warmup_concurrency: usize,

    #[arg(long, default_value = "info", env = "CDN_LOG")]
    pub log_level: String,
}

impl Config {
    pub fn from_args() -> Self {
        Self::parse()
    }
}
