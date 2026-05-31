use serde::Deserialize;
use std::path::Path;

#[derive(Debug, Clone, Copy, Deserialize, PartialEq, Eq, Hash)]
#[serde(rename_all = "lowercase")]
pub enum Protocol {
    Dot,
    Doh,
    DnsCrypt,
}

impl std::fmt::Display for Protocol {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Protocol::Dot => write!(f, "dot"),
            Protocol::Doh => write!(f, "doh"),
            Protocol::DnsCrypt => write!(f, "dnscrypt"),
        }
    }
}

#[derive(Debug, Clone, Deserialize)]
pub struct Config {
    #[serde(default = "default_listen")]
    pub listen: String,
    #[serde(default = "default_metrics")]
    pub metrics_listen: String,
    #[serde(default = "default_cache_max")]
    pub cache_max_entries: usize,
    #[serde(default = "default_prefetch_ratio")]
    pub prefetch_ttl_ratio: f32,
    #[serde(default = "default_prefetch_max")]
    pub prefetch_max_parallel: usize,
    #[serde(default = "default_edns0_buffer")]
    pub edns0_max_buffer_size: usize,
    #[serde(default = "default_tcp_retries")]
    pub tcp_max_retries: usize,
    #[serde(default = "default_tcp_pool_size")]
    pub tcp_pool_max_size: usize,
    #[serde(default = "default_health_check_interval")]
    pub health_check_interval_secs: u64,
    #[serde(default)]
    pub upstreams: Vec<UpstreamConfig>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct UpstreamConfig {
    pub name: String,
    pub address: String,
    #[serde(default = "default_protocol")]
    pub protocol: Protocol,
    #[serde(default)]
    pub provider_name: String,
    #[serde(default)]
    pub provider_public_key: String,
    #[serde(default)]
    pub host: String,
    #[serde(default = "default_weight")]
    #[allow(dead_code)]
    pub weight: u32,
}

fn default_listen() -> String {
    "0.0.0.0:5353".to_string()
}

fn default_metrics() -> String {
    "0.0.0.0:5380".to_string()
}

fn default_cache_max() -> usize {
    10000
}

fn default_prefetch_ratio() -> f32 {
    0.2
}

fn default_prefetch_max() -> usize {
    8
}

fn default_edns0_buffer() -> usize {
    4096
}

fn default_tcp_retries() -> usize {
    3
}

fn default_tcp_pool_size() -> usize {
    8
}

fn default_health_check_interval() -> u64 {
    30
}

fn default_protocol() -> Protocol {
    Protocol::DnsCrypt
}

fn default_weight() -> u32 {
    1
}

impl Config {
    pub fn load(path: &Path) -> anyhow::Result<Self> {
        let text = std::fs::read_to_string(path)?;
        let mut cfg: Config = toml::from_str(&text)?;
        if cfg.upstreams.is_empty() {
            cfg.upstreams.push(UpstreamConfig {
                name: "cloudflare".to_string(),
                address: "1.1.1.1:8443".to_string(),
                protocol: Protocol::DnsCrypt,
                provider_name: "2.dnscrypt.cloudflare-dns.com".to_string(),
                provider_public_key: String::new(),
                host: String::new(),
                weight: 1,
            });
        }
        Ok(cfg)
    }
}
