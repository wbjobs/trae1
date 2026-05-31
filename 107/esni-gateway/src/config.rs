use clap::Parser;
use std::net::SocketAddr;
use std::path::PathBuf;

#[derive(Parser, Debug, Clone)]
#[command(name = "esni-gateway")]
#[command(about = "ESNI Proxy Gateway - High-performance TLS SNI proxy with routing capabilities")]
pub struct Config {
    #[arg(short, long, default_value = "0.0.0.0:443")]
    pub listen: SocketAddr,

    #[arg(short, long, default_value = "4")]
    pub workers: usize,

    #[arg(long, value_name = "FILE")]
    pub block_list: Option<PathBuf>,

    #[arg(long, value_name = "FILE")]
    pub allow_list: Option<PathBuf>,

    #[arg(long, default_value = "false")]
    pub whitelist_mode: bool,

    #[arg(long, default_value = "false")]
    pub dnssec_verify: bool,

    #[arg(long, default_value = "127.0.0.1:9090")]
    pub metrics_addr: SocketAddr,

    #[arg(short, long, default_value = "info")]
    pub log_level: String,

    #[arg(long, default_value = "8192")]
    pub buffer_size: usize,

    #[arg(long, default_value = "30")]
    pub timeout_secs: u64,

    #[arg(long, default_value = "false")]
    pub enable_esni: bool,

    #[arg(long, value_name = "FILE")]
    pub ech_key: Option<PathBuf>,

    #[arg(long, value_name = "FILE")]
    pub ech_failure_log: Option<PathBuf>,

    #[arg(long, default_value = "3600")]
    pub ech_rotation_interval: u64,

    #[arg(long, default_value = "true")]
    pub ech_fallback_enabled: bool,

    #[arg(long, value_name = "FILE")]
    pub fingerprint_db: Option<PathBuf>,

    #[arg(long, default_value = "false")]
    pub enable_fingerprint: bool,

    #[arg(long, default_value = "0.5")]
    pub fingerprint_threshold: f32,

    #[arg(long, default_value = "10000")]
    pub fingerprint_cache_size: usize,

    #[arg(long, default_value = "true")]
    pub fingerprint_use_cache: bool,

    #[arg(long, value_name = "FILE")]
    pub app_database: Option<PathBuf>,
}

impl Config {
    pub fn validate(&self) -> Result<(), String> {
        if self.workers == 0 {
            return Err("workers must be at least 1".to_string());
        }
        if self.buffer_size < 1024 {
            return Err("buffer_size must be at least 1024".to_string());
        }
        if self.whitelist_mode && self.allow_list.is_none() {
            return Err("whitelist_mode requires --allow-list to be specified".to_string());
        }
        if self.fingerprint_threshold < 0.0 || self.fingerprint_threshold > 1.0 {
            return Err("fingerprint_threshold must be between 0.0 and 1.0".to_string());
        }
        Ok(())
    }
}
