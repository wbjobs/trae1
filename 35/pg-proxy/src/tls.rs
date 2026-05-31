use crate::config::{ClientTlsConfig, ServerTlsConfig, TlsConfig};
use parking_lot::RwLock;
use rcgen::{CertificateParams, KeyPair, DistinguishedName, DnType, SanType};
use rustls::{Certificate, PrivateKey, ServerConfig, ClientConfig, RootCertStore};
use rustls_pemfile::{certs, pkcs8_private_keys};
use std::fs;
use std::io::BufReader;
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;
use time::OffsetDateTime;
use tokio::time;
use tracing::{debug, error, info, warn};

#[derive(Debug, Clone)]
pub struct TlsCertificates {
    pub certs: Vec<Certificate>,
    pub key: PrivateKey,
}

impl TlsCertificates {
    pub fn from_files(cert_path: &Path, key_path: &Path) -> Result<Self, String> {
        let certs = load_certs(cert_path)?;
        let key = load_key(key_path)?;
        Ok(Self { certs, key })
    }
}

pub fn load_certs(path: &Path) -> Result<Vec<Certificate>, String> {
    let certfile = fs::File::open(path)
        .map_err(|e| format!("打开证书文件 {} 失败: {}", path.display(), e))?;
    let mut reader = BufReader::new(certfile);
    
    let certs = certs(&mut reader)
        .map_err(|e| format!("解析证书文件 {} 失败: {}", path.display(), e))?;
    
    if certs.is_empty() {
        return Err(format!("证书文件 {} 中没有找到证书", path.display()));
    }
    
    Ok(certs.into_iter().map(Certificate).collect())
}

pub fn load_key(path: &Path) -> Result<PrivateKey, String> {
    let keyfile = fs::File::open(path)
        .map_err(|e| format!("打开密钥文件 {} 失败: {}", path.display(), e))?;
    let mut reader = BufReader::new(keyfile);
    
    let keys = pkcs8_private_keys(&mut reader)
        .map_err(|e| format!("解析密钥文件 {} 失败: {}", path.display(), e))?;
    
    if keys.is_empty() {
        return Err(format!("密钥文件 {} 中没有找到密钥", path.display()));
    }
    
    Ok(PrivateKey(keys[0].clone()))
}

pub fn load_ca_certs(path: &Path) -> Result<RootCertStore, String> {
    let certfile = fs::File::open(path)
        .map_err(|e| format!("打开CA证书文件 {} 失败: {}", path.display(), e))?;
    let mut reader = BufReader::new(certfile);
    
    let certs = certs(&mut reader)
        .map_err(|e| format!("解析CA证书文件 {} 失败: {}", path.display(), e))?;
    
    let mut root_store = RootCertStore::empty();
    for cert in certs {
        root_store
            .add(&Certificate(cert))
            .map_err(|e| format!("添加CA证书失败: {}", e))?;
    }
    
    Ok(root_store)
}

pub fn generate_self_signed_cert(
    cn: &str,
    org: &str,
    valid_days: u32,
) -> Result<(Vec<u8>, Vec<u8>), String> {
    let mut params = CertificateParams::new(vec![cn.to_string()])
        .map_err(|e| format!("创建证书参数失败: {}", e))?;
    
    params.distinguished_name = DistinguishedName::new();
    params.distinguished_name.push(DnType::CommonName, cn);
    params.distinguished_name.push(DnType::OrganizationName, org);
    
    params.subject_alt_names.push(SanType::DnsName("localhost".to_string()));
    params.subject_alt_names.push(SanType::DnsName("127.0.0.1".to_string()));
    
    params.not_after = OffsetDateTime::now_utc()
        + time::Duration::days(valid_days as i64);
    
    let key_pair = KeyPair::generate()
        .map_err(|e| format!("生成密钥对失败: {}", e))?;
    
    let cert = params.self_signed(&key_pair)
        .map_err(|e| format!("生成自签名证书失败: {}", e))?;
    
    let cert_pem = cert.pem();
    let key_pem = key_pair.serialize_pem();
    
    Ok((cert_pem.into_bytes(), key_pem.into_bytes()))
}

pub fn generate_ca_cert(
    cn: &str,
    org: &str,
    valid_days: u32,
) -> Result<(Vec<u8>, Vec<u8>), String> {
    let mut params = CertificateParams::new(vec![cn.to_string()])
        .map_err(|e| format!("创建CA证书参数失败: {}", e))?;
    
    params.distinguished_name = DistinguishedName::new();
    params.distinguished_name.push(DnType::CommonName, cn);
    params.distinguished_name.push(DnType::OrganizationName, org);
    
    params.is_ca = rcgen::IsCa::Ca(rcgen::BasicConstraints::Unconstrained);
    params.key_usages = vec![
        rcgen::KeyUsagePurpose::KeyCertSign,
        rcgen::KeyUsagePurpose::CrlSign,
    ];
    
    params.not_after = OffsetDateTime::now_utc()
        + time::Duration::days(valid_days as i64);
    
    let key_pair = KeyPair::generate()
        .map_err(|e| format!("生成CA密钥对失败: {}", e))?;
    
    let cert = params.self_signed(&key_pair)
        .map_err(|e| format!("生成CA证书失败: {}", e))?;
    
    let cert_pem = cert.pem();
    let key_pem = key_pair.serialize_pem();
    
    Ok((cert_pem.into_bytes(), key_pem.into_bytes()))
}

pub fn save_cert_and_key(
    cert_dir: &Path,
    cert_bytes: &[u8],
    key_bytes: &[u8],
    cert_name: &str,
    key_name: &str,
) -> Result<(String, String), String> {
    fs::create_dir_all(cert_dir)
        .map_err(|e| format!("创建证书目录失败: {}", e))?;
    
    let cert_path = cert_dir.join(cert_name);
    let key_path = cert_dir.join(key_name);
    
    fs::write(&cert_path, cert_bytes)
        .map_err(|e| format!("写入证书文件失败: {}", e))?;
    
    fs::write(&key_path, key_bytes)
        .map_err(|e| format!("写入密钥文件失败: {}", e))?;
    
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let perms = fs::Permissions::from_mode(0o600);
        fs::set_permissions(&key_path, perms)
            .map_err(|e| format!("设置密钥文件权限失败: {}", e))?;
    }
    
    Ok((cert_path.to_string_lossy().to_string(), key_path.to_string_lossy().to_string()))
}

pub fn generate_certificates(
    cert_dir: &Path,
    cn: &str,
    org: &str,
    valid_days: u32,
) -> Result<GeneratedCerts, String> {
    let (ca_cert, ca_key) = generate_ca_cert(cn, org, valid_days)?;
    let (server_cert, server_key) = generate_self_signed_cert(cn, org, valid_days)?;
    let (client_cert, client_key) = generate_self_signed_cert("client", org, valid_days)?;
    
    let (ca_cert_path, ca_key_path) = save_cert_and_key(
        cert_dir, &ca_cert, &ca_key, "ca.crt", "ca.key"
    )?;
    let (server_cert_path, server_key_path) = save_cert_and_key(
        cert_dir, &server_cert, &server_key, "server.crt", "server.key"
    )?;
    let (client_cert_path, client_key_path) = save_cert_and_key(
        cert_dir, &client_cert, &client_key, "client.crt", "client.key"
    )?;
    
    Ok(GeneratedCerts {
        ca_cert: ca_cert_path,
        ca_key: ca_key_path,
        server_cert: server_cert_path,
        server_key: server_key_path,
        client_cert: client_cert_path,
        client_key: client_key_path,
    })
}

#[derive(Debug, Clone)]
pub struct GeneratedCerts {
    pub ca_cert: String,
    pub ca_key: String,
    pub server_cert: String,
    pub server_key: String,
    pub client_cert: String,
    pub client_key: String,
}

#[derive(Clone)]
pub struct TlsContext {
    inner: Arc<TlsContextInner>,
}

struct TlsContextInner {
    client_tls: RwLock<Option<Arc<ServerConfig>>>,
    server_tls: RwLock<Option<Arc<ClientConfig>>>,
    config: TlsConfig,
    instance_name: String,
}

impl TlsContext {
    pub fn new(config: TlsConfig, instance_name: &str) -> Self {
        Self {
            inner: Arc::new(TlsContextInner {
                client_tls: RwLock::new(None),
                server_tls: RwLock::new(None),
                config,
                instance_name: instance_name.to_string(),
            }),
        }
    }

    pub fn reload(&self) -> Result<(), String> {
        let config = &self.inner.config;
        let instance_name = &self.inner.instance_name;
        
        if config.client.enabled {
            let server_config = build_server_config(&config.client)?;
            *self.inner.client_tls.write() = Some(Arc::new(server_config));
            debug!("[{}] 客户端TLS配置已重新加载", instance_name);
        }
        
        if config.server.enabled {
            let client_config = build_client_config(&config.server)?;
            *self.inner.server_tls.write() = Some(Arc::new(client_config));
            debug!("[{}] 服务器TLS配置已重新加载", instance_name);
        }
        
        Ok(())
    }

    pub fn get_server_config(&self) -> Option<Arc<ServerConfig>> {
        self.inner.client_tls.read().clone()
    }

    pub fn get_client_config(&self) -> Option<Arc<ClientConfig>> {
        self.inner.server_tls.read().clone()
    }

    pub fn client_tls_enabled(&self) -> bool {
        self.inner.config.client.enabled
    }

    pub fn server_tls_enabled(&self) -> bool {
        self.inner.config.server.enabled
    }
}

pub fn build_server_config(config: &ClientTlsConfig) -> Result<ServerConfig, String> {
    let cert_path = config
        .cert_file
        .as_ref()
        .ok_or_else(|| "客户端TLS配置缺少cert_file".to_string())?;
    let key_path = config
        .key_file
        .as_ref()
        .ok_or_else(|| "客户端TLS配置缺少key_file".to_string())?;
    
    let certs = load_certs(Path::new(cert_path))?;
    let key = load_key(Path::new(key_path))?;
    
    let mut server_config = ServerConfig::builder()
        .with_safe_defaults()
        .with_no_client_auth();
    
    if config.require_client_cert {
        if let Some(ca_path) = &config.ca_file {
            let client_auth = rustls::server::AllowAnyAuthenticatedClient::new(
                load_ca_certs(Path::new(ca_path))?
            );
            server_config = ServerConfig::builder()
                .with_safe_defaults()
                .with_client_cert_verifier(client_auth);
        }
    }
    
    server_config
        .with_single_cert(certs, key)
        .map_err(|e| format!("配置服务器TLS证书失败: {}", e))?;
    
    Ok(server_config)
}

pub fn build_client_config(config: &ServerTlsConfig) -> Result<ClientConfig, String> {
    let mut root_store = RootCertStore::empty();
    
    if let Some(ca_path) = &config.ca_file {
        root_store = load_ca_certs(Path::new(ca_path))?;
    } else {
        let native_certs = rustls_native_certs::load()
            .map_err(|e| format!("加载系统根证书失败: {}", e))?;
        for cert in native_certs {
            root_store
                .add(&cert)
                .map_err(|e| format!("添加系统根证书失败: {}", e))?;
        }
    }
    
    let builder = ClientConfig::builder()
        .with_safe_defaults()
        .with_root_certificates(root_store);
    
    let client_config = if config.accept_invalid_certs {
        builder
            .dangerous()
            .with_custom_certificate_verifier(Arc::new(
                rustls::client::danger::NoOpServerVerifier::default()
            ))
            .with_no_client_auth()
    } else if let (Some(cert_path), Some(key_path)) = (&config.cert_file, &config.key_file) {
        let certs = load_certs(Path::new(cert_path))?;
        let key = load_key(Path::new(key_path))?;
        builder.with_client_auth_cert(certs, key)
            .map_err(|e| format!("配置客户端TLS证书失败: {}", e))?
    } else {
        builder.with_no_client_auth()
    };
    
    Ok(client_config)
}

pub async fn start_hot_reload_task(
    tls_context: TlsContext,
    instance_name: String,
    interval_secs: u64,
) {
    if interval_secs == 0 {
        return;
    }
    
    info!("[{}] 证书热加载已启动，间隔: {}秒", instance_name, interval_secs);
    
    loop {
        time::sleep(Duration::from_secs(interval_secs)).await;
        
        match tls_context.reload() {
            Ok(_) => debug!("[{}] 证书热加载成功", instance_name),
            Err(e) => warn!("[{}] 证书热加载失败: {}", instance_name, e),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;
    
    #[test]
    fn test_generate_self_signed_cert() {
        let (cert_pem, key_pem) = generate_self_signed_cert("test", "TestOrg", 365).unwrap();
        assert!(!cert_pem.is_empty());
        assert!(!key_pem.is_empty());
        assert!(cert_pem.starts_with(b"-----BEGIN CERTIFICATE-----"));
        assert!(key_pem.starts_with(b"-----BEGIN PRIVATE KEY-----"));
    }
    
    #[test]
    fn test_generate_ca_cert() {
        let (cert_pem, key_pem) = generate_ca_cert("CA", "TestOrg", 365).unwrap();
        assert!(!cert_pem.is_empty());
        assert!(!key_pem.is_empty());
        assert!(cert_pem.starts_with(b"-----BEGIN CERTIFICATE-----"));
        assert!(key_pem.starts_with(b"-----BEGIN PRIVATE KEY-----"));
    }
    
    #[test]
    fn test_save_cert_and_key() {
        let dir = TempDir::new().unwrap();
        let cert_bytes = b"-----BEGIN CERTIFICATE-----\ntest\n-----END CERTIFICATE-----";
        let key_bytes = b"-----BEGIN PRIVATE KEY-----\ntest\n-----END PRIVATE KEY-----";
        
        let (cert_path, key_path) = save_cert_and_key(
            dir.path(), cert_bytes, key_bytes, "test.crt", "test.key"
        ).unwrap();
        
        assert!(Path::new(&cert_path).exists());
        assert!(Path::new(&key_path).exists());
    }
    
    #[test]
    fn test_generate_certificates() {
        let dir = TempDir::new().unwrap();
        let certs = generate_certificates(dir.path(), "test", "TestOrg", 365).unwrap();
        
        assert!(Path::new(&certs.ca_cert).exists());
        assert!(Path::new(&certs.ca_key).exists());
        assert!(Path::new(&certs.server_cert).exists());
        assert!(Path::new(&certs.server_key).exists());
        assert!(Path::new(&certs.client_cert).exists());
        assert!(Path::new(&certs.client_key).exists());
    }
    
    #[test]
    fn test_load_certs() {
        let dir = TempDir::new().unwrap();
        let (cert_pem, _) = generate_self_signed_cert("test", "TestOrg", 365).unwrap();
        
        let cert_path = dir.path().join("test.crt");
        fs::write(&cert_path, &cert_pem).unwrap();
        
        let certs = load_certs(&cert_path).unwrap();
        assert_eq!(certs.len(), 1);
    }
    
    #[test]
    fn test_tls_context() {
        let dir = TempDir::new().unwrap();
        let certs = generate_certificates(dir.path(), "test", "TestOrg", 365).unwrap();
        
        let config = TlsConfig {
            client: ClientTlsConfig {
                enabled: false,
                cert_file: Some(certs.server_cert.clone()),
                key_file: Some(certs.server_key.clone()),
                ca_file: Some(certs.ca_cert.clone()),
                require_client_cert: false,
                min_version: "TLSv1.3".to_string(),
            },
            server: ServerTlsConfig {
                enabled: false,
                cert_file: Some(certs.client_cert.clone()),
                key_file: Some(certs.client_key.clone()),
                ca_file: Some(certs.ca_cert.clone()),
                verify_server_cert: false,
                accept_invalid_certs: true,
                min_version: "TLSv1.3".to_string(),
            },
            cert_dir: dir.path().to_string_lossy().to_string(),
            hot_reload: false,
            reload_interval_secs: 0,
        };
        
        let context = TlsContext::new(config, "test");
        assert!(!context.client_tls_enabled());
        assert!(!context.server_tls_enabled());
        assert!(context.get_server_config().is_none());
        assert!(context.get_client_config().is_none());
    }
}
