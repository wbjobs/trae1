use crate::config::UpstreamConfig;
use crate::stats::Stats;
use anyhow::{anyhow, Context, Result};
use byteorder::{BigEndian, ByteOrder};
use rand::RngCore;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpStream, UdpSocket};
use tokio::sync::Mutex as AsyncMutex;
use tokio::time::timeout;
use trust_dns_proto::op::{Edns, Message, OpCode};
use trust_dns_proto::rr::{Name, RData, RecordType};
use trust_dns_proto::serialize::binary::{BinDecodable, BinEncodable};
use x25519_dalek::{PublicKey as X25519PublicKey, StaticSecret as X25519Secret};
use xsalsa20poly1305::aead::Aead;
use xsalsa20poly1305::KeyInit;

pub const CERT_MAGIC: &[u8; 8] = b"DNSC\0\0\0\0";
#[allow(dead_code)]
pub const CLIENT_MAGIC: &[u8; 8] = b"Q6fnvWj8";
pub const RESOLVER_MAGIC: &[u8; 8] = b"r6fnvWj8";

#[derive(Clone, Debug)]
pub struct ResolverCert {
    pub resolver_public_key: [u8; 32],
    #[allow(dead_code)]
    pub serial: u32,
    #[allow(dead_code)]
    pub ts_start: u32,
    #[allow(dead_code)]
    pub ts_end: u32,
    pub client_magic: [u8; 8],
}

pub struct TcpPool {
    address: String,
    idle: AsyncMutex<Vec<TcpStream>>,
    max_size: usize,
}

impl TcpPool {
    pub fn new(address: String, max_size: usize) -> Self {
        Self {
            address,
            idle: AsyncMutex::new(Vec::new()),
            max_size: max_size.max(1),
        }
    }

    pub async fn acquire(&self) -> Result<TcpStream> {
        let mut guard = self.idle.lock().await;
        while let Some(stream) = guard.pop() {
            if stream.peer_addr().is_ok() {
                return Ok(stream);
            }
        }
        drop(guard);
        let stream = TcpStream::connect(&self.address)
            .await
            .with_context(|| format!("TCP 连接上游失败: {}", self.address))?;
        Ok(stream)
    }

    pub async fn release(&self, stream: TcpStream) {
        let mut guard = self.idle.lock().await;
        if guard.len() < self.max_size {
            guard.push(stream);
        }
    }
}

pub struct DnsCryptUpstream {
    pub name: String,
    pub address: String,
    pub provider_name: String,
    pub provider_public_key: [u8; 32],
    pub cert: AsyncMutex<Option<ResolverCert>>,
    pub socket: AsyncMutex<Option<Arc<UdpSocket>>>,
    pub tcp_pool: TcpPool,
    pub stats: Stats,
    pub cert_refresh_at: AsyncMutex<Instant>,
    pub healthy: AsyncMutex<bool>,
    pub edns0_buffer_size: u16,
    pub tcp_max_retries: usize,
}

impl DnsCryptUpstream {
    pub fn new(
        cfg: &UpstreamConfig,
        stats: Stats,
        edns0_buffer_size: u16,
        tcp_max_retries: usize,
        tcp_pool_max_size: usize,
    ) -> Result<Self> {
        let key_bytes = parse_hex_key(&cfg.provider_public_key)
            .with_context(|| format!("解析 provider_public_key 失败: {}", cfg.provider_public_key))?;
        let tcp_pool = TcpPool::new(cfg.address.clone(), tcp_pool_max_size);
        Ok(Self {
            name: cfg.name.clone(),
            address: cfg.address.clone(),
            provider_name: cfg.provider_name.clone(),
            provider_public_key: key_bytes,
            cert: AsyncMutex::new(None),
            socket: AsyncMutex::new(None),
            tcp_pool,
            stats,
            cert_refresh_at: AsyncMutex::new(Instant::now()),
            healthy: AsyncMutex::new(true),
            edns0_buffer_size,
            tcp_max_retries,
        })
    }

    async fn socket(&self) -> Result<Arc<UdpSocket>> {
        let mut guard = self.socket.lock().await;
        if let Some(s) = guard.clone() {
            return Ok(s);
        }
        let s = UdpSocket::bind("0.0.0.0:0").await?;
        s.connect(&self.address).await?;
        let arc = Arc::new(s);
        *guard = Some(arc.clone());
        Ok(arc)
    }

    async fn ensure_cert(&self) -> Result<ResolverCert> {
        if let Some(cert) = self.cert.lock().await.clone() {
            if Instant::now() < *self.cert_refresh_at.lock().await {
                return Ok(cert);
            }
        }
        let cert = self.fetch_and_verify_cert().await?;
        let refresh_at = Instant::now() + Duration::from_secs(3600);
        *self.cert.lock().await = Some(cert.clone());
        *self.cert_refresh_at.lock().await = refresh_at;
        Ok(cert)
    }

    async fn fetch_and_verify_cert(&self) -> Result<ResolverCert> {
        let qname = if self.provider_name.starts_with("2.") {
            format!("{}.", self.provider_name)
        } else {
            format!("2.{}.", self.provider_name)
        };
        let name = Name::from_utf8(&qname)
            .with_context(|| format!("解析 provider_name 失败: {}", self.provider_name))?;

        let mut msg = Message::new();
        msg.set_id(rand::random::<u16>());
        msg.set_op_code(OpCode::Query);
        msg.set_recursion_desired(true);
        let query = trust_dns_proto::op::Query::query(name, RecordType::TXT);
        msg.add_query(query);

        let req_bytes = msg.to_bytes()?;

        let sock = UdpSocket::bind("0.0.0.0:0").await?;
        sock.connect(&self.address).await?;
        sock.send(&req_bytes).await?;

        let mut buf = vec![0u8; 4096];
        let (len, _) = timeout(Duration::from_secs(5), sock.recv_from(&mut buf))
            .await
            .context("获取 DNSCrypt 证书超时")??;
        buf.truncate(len);

        let response = Message::from_bytes(&buf).context("解析证书响应失败")?;

        let mut candidates: Vec<Vec<u8>> = Vec::new();
        for rec in response.answers() {
            if let Some(RData::TXT(txt)) = rec.data() {
                let txt_data = txt.txt_data();
                if !txt_data.is_empty() {
                    let combined: Vec<u8> = txt_data.iter().flat_map(|b| b.iter().copied()).collect();
                    candidates.push(combined);
                }
            }
        }

        for data in candidates {
            if data.len() < 72 {
                continue;
            }
            if &data[0..8] != CERT_MAGIC {
                continue;
            }
            if &data[8..10] != b"\x00\x01" {
                continue;
            }

            let signed = &data[8..124];
            let signature = &data[124..188];

            let verifying_key = ed25519_dalek::VerifyingKey::from_bytes(&self.provider_public_key)
                .context("无效的 Ed25519 公钥")?;
            let sig = ed25519_dalek::Signature::from_slice(signature)
                .context("签名长度错误")?;
            if verifying_key.verify_strict(signed, &sig).is_err() {
                tracing::warn!(upstream = %self.name, "证书 Ed25519 签名验证失败，跳过");
                continue;
            }

            let client_magic = {
                let mut m = [0u8; 8];
                m.copy_from_slice(&data[36..44]);
                m
            };

            let serial = BigEndian::read_u32(&data[44..48]);
            let ts_start = BigEndian::read_u32(&data[48..52]);
            let ts_end = BigEndian::read_u32(&data[52..56]);

            let mut resolver_pk = [0u8; 32];
            resolver_pk.copy_from_slice(&data[84..116]);

            return Ok(ResolverCert {
                resolver_public_key: resolver_pk,
                serial,
                ts_start,
                ts_end,
                client_magic,
            });
        }

        Err(anyhow!("未找到有效的 DNSCrypt 证书"))
    }

    pub async fn query(&self, message: &Message) -> Result<Message> {
        let start = Instant::now();

        let udp_result = self.query_udp(message).await;
        match udp_result {
            Ok(response) => {
                if response.header().truncated() {
                    tracing::debug!(
                        upstream = %self.name,
                        "UDP 响应被截断 (TC)，通过 TCP 重试"
                    );
                    let tcp_result = self.query_tcp(message).await;
                    self.stats
                        .record_upstream(&self.name, start, tcp_result.is_ok());
                    if tcp_result.is_ok() {
                        *self.healthy.lock().await = true;
                    } else {
                        *self.healthy.lock().await = false;
                    }
                    return tcp_result;
                }
                self.stats.record_upstream(&self.name, start, true);
                *self.healthy.lock().await = true;
                Ok(response)
            }
            Err(e) => {
                tracing::debug!(
                    upstream = %self.name,
                    error = %e,
                    "UDP 查询失败，通过 TCP 重试"
                );
                let tcp_result = self.query_tcp(message).await;
                self.stats
                    .record_upstream(&self.name, start, tcp_result.is_ok());
                if tcp_result.is_ok() {
                    *self.healthy.lock().await = true;
                } else {
                    *self.healthy.lock().await = false;
                }
                tcp_result
            }
        }
    }

    fn build_edns0_query(&self, msg: &Message) -> Message {
        let mut m = msg.clone();
        if m.extensions().is_none() {
            let mut edns = Edns::new();
            edns.set_max_payload(self.edns0_buffer_size);
            edns.set_version(0);
            m.set_edns(edns);
        }
        m
    }

    async fn encrypt_query(
        &self,
        message: &Message,
    ) -> Result<(Vec<u8>, xsalsa20poly1305::XSalsa20Poly1305, [u8; 24])> {
        let cert = self.ensure_cert().await?;

        let (client_nonce, sk, pk) = {
            let mut rng = rand::thread_rng();
            let mut cn = [0u8; 24];
            rng.fill_bytes(&mut cn);
            let s = X25519Secret::random_from_rng(&mut rng);
            let p = X25519PublicKey::from(&s);
            (cn, s, p)
        };

        let resolver_pk = X25519PublicKey::from(cert.resolver_public_key);
        let shared = sk.diffie_hellman(&resolver_pk);
        let key_bytes = derive_encryption_key(shared.as_bytes());

        let query_bytes = message.to_bytes()?;
        let padded = pad_query(&query_bytes);

        let cipher = xsalsa20poly1305::XSalsa20Poly1305::new(&key_bytes.into());
        let nonce = xsalsa20poly1305::Nonce::from_slice(&client_nonce);
        let encrypted = cipher
            .encrypt(nonce, padded.as_ref())
            .map_err(|e| anyhow!("加密失败: {}", e))?;

        let mut packet = Vec::with_capacity(8 + 32 + 24 + encrypted.len());
        packet.extend_from_slice(&cert.client_magic);
        packet.extend_from_slice(pk.as_bytes());
        packet.extend_from_slice(&client_nonce);
        packet.extend_from_slice(&encrypted);

        Ok((packet, cipher, client_nonce))
    }

    fn decrypt_response(
        &self,
        raw: &[u8],
        cipher: &xsalsa20poly1305::XSalsa20Poly1305,
        client_nonce: &[u8; 24],
    ) -> Result<Message> {
        if raw.len() < 8 + 24 + 16 {
            return Err(anyhow!("DNSCrypt 响应包过短"));
        }
        if &raw[0..8] != RESOLVER_MAGIC {
            return Err(anyhow!("DNSCrypt 响应 magic 不匹配"));
        }
        let mut nonce_bytes = [0u8; 24];
        nonce_bytes.copy_from_slice(&raw[8..32]);
        if &nonce_bytes[..12] != &client_nonce[..12] {
            return Err(anyhow!("DNSCrypt 响应 nonce 不匹配"));
        }

        let encrypted = &raw[32..];
        let decrypted = cipher
            .decrypt(
                xsalsa20poly1305::Nonce::from_slice(&nonce_bytes),
                encrypted,
            )
            .map_err(|_| anyhow!("DNSCrypt 响应解密失败"))?;

        let unpadded = unpad_query(&decrypted).context("DNSCrypt 响应填充错误")?;
        let msg = Message::from_bytes(unpadded).context("解析解密后的 DNS 响应失败")?;
        Ok(msg)
    }

    async fn query_udp(&self, message: &Message) -> Result<Message> {
        let msg = self.build_edns0_query(message);
        let (packet, cipher, client_nonce) = self.encrypt_query(&msg).await?;

        let sock = self.socket().await?;
        sock.send(&packet).await?;

        let mut buf = vec![0u8; 65535];
        let (len, _) = timeout(Duration::from_secs(5), sock.recv_from(&mut buf))
            .await
            .context("DNSCrypt UDP 查询超时")??;
        let raw = &buf[..len];

        self.decrypt_response(raw, &cipher, &client_nonce)
    }

    async fn query_tcp(&self, message: &Message) -> Result<Message> {
        let msg = self.build_edns0_query(message);
        let mut last_err: Option<anyhow::Error> = None;

        for attempt in 0..self.tcp_max_retries {
            let (packet, cipher, client_nonce) = match self.encrypt_query(&msg).await {
                Ok(p) => p,
                Err(e) => {
                    last_err = Some(e);
                    break;
                }
            };

            let mut stream = match self.tcp_pool.acquire().await {
                Ok(s) => s,
                Err(e) => {
                    tracing::debug!(
                        upstream = %self.name,
                        attempt = attempt + 1,
                        error = %e,
                        "TCP 连接失败"
                    );
                    last_err = Some(e);
                    continue;
                }
            };

            let len_prefix = (packet.len() as u16).to_be_bytes();
            let write_result = stream.write_all(&len_prefix).await;
            if write_result.is_err() {
                last_err = write_result.map_err(|e| anyhow!("TCP 写入长度失败: {}", e)).err();
                continue;
            }
            let write_result = stream.write_all(&packet).await;
            if write_result.is_err() {
                last_err = write_result.map_err(|e| anyhow!("TCP 写入数据失败: {}", e)).err();
                continue;
            }

            let mut len_buf = [0u8; 2];
            let read_result = timeout(Duration::from_secs(10), stream.read_exact(&mut len_buf)).await;
            let resp_len = match read_result {
                Ok(Ok(_)) => u16::from_be_bytes(len_buf) as usize,
                Ok(Err(e)) => {
                    last_err = Some(anyhow!("TCP 读取长度失败: {}", e));
                    continue;
                }
                Err(_) => {
                    last_err = Some(anyhow!("TCP 读取长度超时"));
                    continue;
                }
            };

            if resp_len == 0 || resp_len > 65535 {
                last_err = Some(anyhow!("无效的 TCP 响应长度: {}", resp_len));
                continue;
            }

            let mut resp_buf = vec![0u8; resp_len];
            let read_result = timeout(Duration::from_secs(10), stream.read_exact(&mut resp_buf)).await;
            match read_result {
                Ok(Ok(_)) => {}
                Ok(Err(e)) => {
                    last_err = Some(anyhow!("TCP 读取响应失败: {}", e));
                    continue;
                }
                Err(_) => {
                    last_err = Some(anyhow!("TCP 读取响应超时"));
                    continue;
                }
            }

            match self.decrypt_response(&resp_buf, &cipher, &client_nonce) {
                Ok(msg) => {
                    self.tcp_pool.release(stream).await;
                    return Ok(msg);
                }
                Err(e) => {
                    last_err = Some(e);
                    continue;
                }
            }
        }

        Err(last_err.unwrap_or_else(|| anyhow!("TCP 查询失败")))
    }

    pub async fn is_healthy(&self) -> bool {
        *self.healthy.lock().await
    }
}

#[allow(dead_code)]
pub struct UpstreamPool {
    pub upstreams: Vec<Arc<DnsCryptUpstream>>,
    pub active_index: AsyncMutex<usize>,
    pub stats: Stats,
}

#[allow(dead_code)]
impl UpstreamPool {
    pub fn new(upstreams: Vec<Arc<DnsCryptUpstream>>, stats: Stats) -> Self {
        Self {
            upstreams,
            active_index: AsyncMutex::new(0),
            stats,
        }
    }

    #[allow(dead_code)]
    pub fn first_upstream_name(&self) -> String {
        self.upstreams
            .first()
            .map(|u| u.name.clone())
            .unwrap_or_default()
    }

    pub async fn query(&self, message: &Message) -> Result<Message> {
        let idx = *self.active_index.lock().await;
        let order: Vec<usize> = (0..self.upstreams.len())
            .map(|i| (idx + i) % self.upstreams.len())
            .collect();

        let mut last_err: Option<anyhow::Error> = None;
        for i in order {
            let u = &self.upstreams[i];
            if !u.is_healthy().await {
                if let Ok(msg) = u.query(message).await {
                    *self.active_index.lock().await = i;
                    self.stats.set_active(&u.name);
                    return Ok(msg);
                }
                continue;
            }
            match u.query(message).await {
                Ok(msg) => {
                    *self.active_index.lock().await = i;
                    self.stats.set_active(&u.name);
                    return Ok(msg);
                }
                Err(e) => {
                    tracing::warn!(upstream = %u.name, error = %e, "上游 DNSCrypt 查询失败，尝试下一个");
                    last_err = Some(e);
                }
            }
        }
        Err(last_err.unwrap_or_else(|| anyhow!("所有上游均不可用")))
    }
}

fn parse_hex_key(s: &str) -> Result<[u8; 32]> {
    let cleaned: String = s.chars().filter(|c| c.is_ascii_hexdigit()).collect();
    if cleaned.len() != 64 {
        return Err(anyhow!("公钥长度应为 32 字节 (64 hex)"));
    }
    let bytes = hex::decode(&cleaned).context("hex 解码失败")?;
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    Ok(out)
}

fn pad_query(input: &[u8]) -> Vec<u8> {
    let block = 64usize;
    let min_pad = 1usize;
    let total = input.len() + min_pad;
    let padded_len = ((total + block - 1) / block) * block;
    let mut out = Vec::with_capacity(padded_len);
    out.extend_from_slice(input);
    out.push(0x80);
    while out.len() < padded_len {
        out.push(0x00);
    }
    out
}

fn unpad_query(input: &[u8]) -> Result<&[u8]> {
    for i in (0..input.len()).rev() {
        if input[i] == 0x80 {
            return Ok(&input[..i]);
        }
        if input[i] != 0x00 {
            return Err(anyhow!("无效的填充字节"));
        }
    }
    Err(anyhow!("未找到填充标记"))
}

fn derive_encryption_key(shared_secret: &[u8]) -> [u8; 32] {
    use sha2::{Digest, Sha512};
    let mut hasher = Sha512::new();
    hasher.update(b"DNSCrypt");
    hasher.update(shared_secret);
    let digest = hasher.finalize();
    let mut key = [0u8; 32];
    key.copy_from_slice(&digest[..32]);
    key
}
