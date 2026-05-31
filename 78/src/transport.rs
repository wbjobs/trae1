use crate::config::{Protocol, UpstreamConfig};
use crate::stats::Stats;
use anyhow::{anyhow, Context, Result};
use async_trait::async_trait;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio::sync::Mutex as AsyncMutex;
use tokio_rustls::rustls::ClientConfig;
use tokio_rustls::TlsConnector;
use tokio::time::timeout;
use trust_dns_proto::op::{Edns, Message};
use trust_dns_proto::serialize::binary::{BinDecodable, BinEncodable};

use crate::dnscrypt::DnsCryptUpstream;

#[async_trait]
pub trait DnsTransport: Send + Sync {
    async fn query(&self, msg: &Message) -> Result<Message>;
    fn name(&self) -> &str;
    fn protocol(&self) -> Protocol;
    async fn is_healthy(&self) -> bool;
    async fn set_healthy(&self, healthy: bool);
    async fn health_probe(&self) -> Option<f64>;
}

pub async fn build_transports(
    configs: &[UpstreamConfig],
    stats: Stats,
    edns0_buffer: u16,
    tcp_max_retries: usize,
    tcp_pool_max_size: usize,
    health_check_interval: u64,
) -> Result<Vec<Arc<dyn DnsTransport>>> {
    let mut transports: Vec<Arc<dyn DnsTransport>> = Vec::with_capacity(configs.len());

    let client_config = Arc::new(build_tls_config()?);

    for cfg in configs {
        let t: Arc<dyn DnsTransport> = match cfg.protocol {
            Protocol::Dot => {
                let host = if cfg.host.is_empty() {
                    cfg.address.split(':').next().unwrap_or("").to_string()
                } else {
                    cfg.host.clone()
                };
                Arc::new(DotTransport::new(
                    &cfg.name,
                    &cfg.address,
                    &host,
                    edns0_buffer,
                    tcp_max_retries,
                    tcp_pool_max_size,
                    stats.clone(),
                    client_config.clone(),
                ))
            }
            Protocol::Doh => {
                let host = if cfg.host.is_empty() {
                    cfg.address.split(':').next().unwrap_or("").to_string()
                } else {
                    cfg.host.clone()
                };
                let scheme = if cfg.address.contains(':') && cfg.address.ends_with("443") {
                    "https"
                } else {
                    "https"
                };
                let url = if cfg.address.starts_with("http://") || cfg.address.starts_with("https://")
                {
                    format!("{}/dns-query", cfg.address.trim_end_matches('/'))
                } else {
                    format!("{}://{}/dns-query", scheme, cfg.address)
                };
                Arc::new(DohTransport::new(
                    &cfg.name,
                    &url,
                    &host,
                    edns0_buffer,
                    stats.clone(),
                    client_config.clone(),
                )?)
            }
            Protocol::DnsCrypt => Arc::new(DnsCryptTransport::new(
                DnsCryptUpstream::new(
                    cfg,
                    stats.clone(),
                    edns0_buffer,
                    tcp_max_retries,
                    tcp_pool_max_size,
                )?,
                health_check_interval,
            )),
        };
        transports.push(t);
    }

    Ok(transports)
}

fn build_tls_config() -> Result<ClientConfig> {
    let root_certs = {
        let mut roots = rustls::RootCertStore::empty();
        for cert in webpki_roots::TLS_SERVER_ROOTS.iter() {
            let der = rustls::pki_types::CertificateDer::from(cert.subject.as_ref().to_vec());
            roots.add(der).ok();
        }
        roots
    };
    let config = ClientConfig::builder()
        .with_root_certificates(root_certs)
        .with_no_client_auth();
    Ok(config)
}

fn build_edns0_query(msg: &Message, buffer_size: u16) -> Message {
    let mut m = msg.clone();
    if m.extensions().is_none() {
        let mut edns = Edns::new();
        edns.set_max_payload(buffer_size);
        edns.set_version(0);
        m.set_edns(edns);
    }
    m
}

// ---- DoT ----

pub struct DotTransport {
    name: String,
    address: String,
    host: String,
    edns0_buffer_size: u16,
    tcp_max_retries: usize,
    tcp_pool: AsyncMutex<Vec<tokio_rustls::client::TlsStream<TcpStream>>>,
    tcp_pool_max: usize,
    tls_connector: TlsConnector,
    server_name: tokio_rustls::rustls::pki_types::ServerName<'static>,
    stats: Stats,
    healthy: AsyncMutex<bool>,
}

impl DotTransport {
    pub fn new(
        name: &str,
        address: &str,
        host: &str,
        edns0_buffer_size: u16,
        tcp_max_retries: usize,
        tcp_pool_max_size: usize,
        stats: Stats,
        client_config: Arc<ClientConfig>,
    ) -> Self {
        let tls_connector = TlsConnector::from(client_config);
        let server_name = tokio_rustls::rustls::pki_types::ServerName::try_from(host.to_string())
            .unwrap_or_else(|_| {
                tokio_rustls::rustls::pki_types::ServerName::try_from("localhost").unwrap()
            });
        Self {
            name: name.to_string(),
            address: address.to_string(),
            host: host.to_string(),
            edns0_buffer_size,
            tcp_max_retries,
            tcp_pool: AsyncMutex::new(Vec::new()),
            tcp_pool_max: tcp_pool_max_size.max(1),
            tls_connector,
            server_name,
            stats,
            healthy: AsyncMutex::new(true),
        }
    }

    async fn acquire_tls(&self) -> Result<tokio_rustls::client::TlsStream<TcpStream>> {
        let mut pool = self.tcp_pool.lock().await;
        while let Some(stream) = pool.pop() {
            drop(pool);
            return Ok(stream);
        }
        drop(pool);

        let tcp = TcpStream::connect(&self.address)
            .await
            .with_context(|| format!("DoT TCP 连接失败: {}", self.address))?;
        let tls = self
            .tls_connector
            .connect(self.server_name.clone(), tcp)
            .await
            .context("DoT TLS 握手失败")?;
        Ok(tls)
    }

    async fn release_tls(&self, stream: tokio_rustls::client::TlsStream<TcpStream>) {
        let mut pool = self.tcp_pool.lock().await;
        if pool.len() < self.tcp_pool_max {
            pool.push(stream);
        }
    }

    async fn query_dot_inner(&self, msg: &Message) -> Result<Message> {
        let msg = build_edns0_query(msg, self.edns0_buffer_size);
        let query_bytes = msg.to_bytes()?;

        for attempt in 0..self.tcp_max_retries {
            let mut stream = match self.acquire_tls().await {
                Ok(s) => s,
                Err(e) => {
                    if attempt + 1 < self.tcp_max_retries {
                        tracing::debug!(upstream = %self.name, attempt = attempt + 1, error = %e, "DoT 连接失败，重试");
                        continue;
                    }
                    return Err(e);
                }
            };

            let len_prefix = (query_bytes.len() as u16).to_be_bytes();
            if stream.write_all(&len_prefix).await.is_err()
                || stream.write_all(&query_bytes).await.is_err()
            {
                continue;
            }

            let mut len_buf = [0u8; 2];
            let resp_len = match timeout(Duration::from_secs(10), stream.read_exact(&mut len_buf)).await
            {
                Ok(Ok(_)) => u16::from_be_bytes(len_buf) as usize,
                _ => continue,
            };

            if resp_len == 0 || resp_len > 65535 {
                continue;
            }

            let mut resp_buf = vec![0u8; resp_len];
            match timeout(Duration::from_secs(10), stream.read_exact(&mut resp_buf)).await {
                Ok(Ok(_)) => {}
                _ => continue,
            }

            self.release_tls(stream).await;
            return Message::from_bytes(&resp_buf).context("DoT 解析响应失败");
        }
        Err(anyhow!("DoT 查询失败，已达最大重试次数"))
    }
}

#[async_trait]
impl DnsTransport for DotTransport {
    async fn query(&self, msg: &Message) -> Result<Message> {
        let start = Instant::now();
        let result = self.query_dot_inner(msg).await;
        self.stats
            .record_upstream(&self.name, start, result.is_ok());
        if result.is_err() {
            *self.healthy.lock().await = false;
        } else {
            *self.healthy.lock().await = true;
        }
        result
    }
    fn name(&self) -> &str {
        &self.name
    }
    fn protocol(&self) -> Protocol {
        Protocol::Dot
    }
    async fn is_healthy(&self) -> bool {
        *self.healthy.lock().await
    }
    async fn set_healthy(&self, healthy: bool) {
        *self.healthy.lock().await = healthy;
    }
    async fn health_probe(&self) -> Option<f64> {
        let mut probe = Message::new();
        probe.set_id(rand::random::<u16>());
        probe.set_recursion_desired(true);
        let name = trust_dns_proto::rr::Name::from_utf8("cloudflare.com.").ok()?;
        let query = trust_dns_proto::op::Query::query(name, trust_dns_proto::rr::RecordType::A);
        probe.add_query(query);

        let start = Instant::now();
        match timeout(Duration::from_secs(5), self.query_dot_inner(&probe)).await {
            Ok(Ok(_)) => Some(start.elapsed().as_secs_f64() * 1000.0),
            _ => None,
        }
    }
}

// ---- DoH ----

pub struct DohTransport {
    name: String,
    url: String,
    host: String,
    edns0_buffer_size: u16,
    client: reqwest::Client,
    stats: Stats,
    healthy: AsyncMutex<bool>,
}

impl DohTransport {
    pub fn new(
        name: &str,
        url: &str,
        host: &str,
        edns0_buffer_size: u16,
        stats: Stats,
        _client_config: Arc<ClientConfig>,
    ) -> Result<Self> {
        let client = reqwest::Client::builder()
            .build()
            .context("创建 DoH HTTP 客户端失败")?;
        Ok(Self {
            name: name.to_string(),
            url: url.to_string(),
            host: host.to_string(),
            edns0_buffer_size,
            client,
            stats,
            healthy: AsyncMutex::new(true),
        })
    }

    async fn query_doh_inner(&self, msg: &Message) -> Result<Message> {
        let msg = build_edns0_query(msg, self.edns0_buffer_size);
        let query_bytes = msg.to_bytes()?;

        let resp = self
            .client
            .post(&self.url)
            .header("content-type", "application/dns-message")
            .header("accept", "application/dns-message")
            .body(query_bytes)
            .send()
            .await
            .context("DoH HTTP 请求失败")?;

        if !resp.status().is_success() {
            return Err(anyhow!("DoH HTTP 状态码: {}", resp.status()));
        }

        let body = resp.bytes().await.context("DoH 读取响应体失败")?;
        Message::from_bytes(&body).context("DoH 解析响应失败")
    }
}

#[async_trait]
impl DnsTransport for DohTransport {
    async fn query(&self, msg: &Message) -> Result<Message> {
        let start = Instant::now();
        let result = self.query_doh_inner(msg).await;
        self.stats
            .record_upstream(&self.name, start, result.is_ok());
        if result.is_err() {
            *self.healthy.lock().await = false;
        } else {
            *self.healthy.lock().await = true;
        }
        result
    }
    fn name(&self) -> &str {
        &self.name
    }
    fn protocol(&self) -> Protocol {
        Protocol::Doh
    }
    async fn is_healthy(&self) -> bool {
        *self.healthy.lock().await
    }
    async fn set_healthy(&self, healthy: bool) {
        *self.healthy.lock().await = healthy;
    }
    async fn health_probe(&self) -> Option<f64> {
        let mut probe = Message::new();
        probe.set_id(rand::random::<u16>());
        probe.set_recursion_desired(true);
        let name = trust_dns_proto::rr::Name::from_utf8("cloudflare.com.").ok()?;
        let query = trust_dns_proto::op::Query::query(name, trust_dns_proto::rr::RecordType::A);
        probe.add_query(query);

        let start = Instant::now();
        match timeout(Duration::from_secs(5), self.query_doh_inner(&probe)).await {
            Ok(Ok(_)) => Some(start.elapsed().as_secs_f64() * 1000.0),
            _ => None,
        }
    }
}

// ---- DNSCrypt Adapter ----

pub struct DnsCryptTransport {
    inner: Arc<DnsCryptUpstream>,
    health_check_interval: u64,
}

impl DnsCryptTransport {
    pub fn new(inner: DnsCryptUpstream, health_check_interval: u64) -> Self {
        Self {
            inner: Arc::new(inner),
            health_check_interval,
        }
    }
}

#[async_trait]
impl DnsTransport for DnsCryptTransport {
    async fn query(&self, msg: &Message) -> Result<Message> {
        self.inner.query(msg).await
    }
    fn name(&self) -> &str {
        &self.inner.name
    }
    fn protocol(&self) -> Protocol {
        Protocol::DnsCrypt
    }
    async fn is_healthy(&self) -> bool {
        self.inner.is_healthy().await
    }
    async fn set_healthy(&self, healthy: bool) {
        *self.inner.healthy.lock().await = healthy;
    }
    async fn health_probe(&self) -> Option<f64> {
        let mut probe = Message::new();
        probe.set_id(rand::random::<u16>());
        probe.set_recursion_desired(true);
        let name = trust_dns_proto::rr::Name::from_utf8("cloudflare.com.").ok()?;
        let query = trust_dns_proto::op::Query::query(name, trust_dns_proto::rr::RecordType::A);
        probe.add_query(query);

        let start = Instant::now();
        match timeout(
            Duration::from_secs(self.health_check_interval),
            self.inner.query(&probe),
        )
        .await
        {
            Ok(Ok(_)) => Some(start.elapsed().as_secs_f64() * 1000.0),
            _ => None,
        }
    }
}

// ---- Upstream Pool with Protocol Fallback ----

pub struct UpstreamPool {
    pub transports: Vec<Arc<dyn DnsTransport>>,
    pub active_index: AsyncMutex<usize>,
    pub stats: Stats,
}

impl UpstreamPool {
    pub fn new(transports: Vec<Arc<dyn DnsTransport>>, stats: Stats) -> Self {
        Self {
            transports,
            active_index: AsyncMutex::new(0),
            stats,
        }
    }

    pub async fn query(&self, message: &Message) -> Result<Message> {
        let protocol_priority: Vec<Protocol> =
            vec![Protocol::Dot, Protocol::Doh, Protocol::DnsCrypt];

        let mut last_err: Option<anyhow::Error> = None;
        let mut last_name = String::new();

        for proto in &protocol_priority {
            let candidates: Vec<(usize, &Arc<dyn DnsTransport>)> = self
                .transports
                .iter()
                .enumerate()
                .filter(|(_, t)| t.protocol() == *proto)
                .collect();

            if candidates.is_empty() {
                continue;
            }

            for (idx, transport) in candidates {
                if !transport.is_healthy().await {
                    if let Ok(msg) = transport.query(message).await {
                        *self.active_index.lock().await = idx;
                        self.stats.set_active(transport.name());
                        return Ok(msg);
                    }
                    continue;
                }
                match transport.query(message).await {
                    Ok(msg) => {
                        *self.active_index.lock().await = idx;
                        self.stats.set_active(transport.name());
                        return Ok(msg);
                    }
                    Err(e) => {
                        last_name = transport.name().to_string();
                        tracing::debug!(
                            upstream = %transport.name(),
                            protocol = %transport.protocol(),
                            error = %e,
                            "上游查询失败，尝试下一协议/上游"
                        );
                        last_err = Some(e);
                    }
                }
            }
        }

        Err(last_err.unwrap_or_else(|| {
            anyhow!(
                "所有上游均不可用 (active was: {})",
                if last_name.is_empty() {
                    "none"
                } else {
                    &last_name
                }
            )
        }))
    }
}