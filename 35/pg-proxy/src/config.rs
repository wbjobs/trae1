use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CompressColumnConfig {
    pub table: String,
    pub columns: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ClientTlsConfig {
    #[serde(default)]
    pub enabled: bool,
    #[serde(default)]
    pub cert_file: Option<String>,
    #[serde(default)]
    pub key_file: Option<String>,
    #[serde(default)]
    pub ca_file: Option<String>,
    #[serde(default = "default_true")]
    pub require_client_cert: bool,
    #[serde(default = "default_min_tls_version")]
    pub min_version: String,
}

fn default_min_tls_version() -> String {
    "TLSv1.3".to_string()
}

impl Default for ClientTlsConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            cert_file: None,
            key_file: None,
            ca_file: None,
            require_client_cert: false,
            min_version: "TLSv1.3".to_string(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ServerTlsConfig {
    #[serde(default)]
    pub enabled: bool,
    #[serde(default)]
    pub cert_file: Option<String>,
    #[serde(default)]
    pub key_file: Option<String>,
    #[serde(default)]
    pub ca_file: Option<String>,
    #[serde(default = "default_true")]
    pub verify_server_cert: bool,
    #[serde(default)]
    pub accept_invalid_certs: bool,
    #[serde(default = "default_min_tls_version")]
    pub min_version: String,
}

impl Default for ServerTlsConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            cert_file: None,
            key_file: None,
            ca_file: None,
            verify_server_cert: true,
            accept_invalid_certs: false,
            min_version: "TLSv1.3".to_string(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TlsConfig {
    #[serde(default)]
    pub client: ClientTlsConfig,
    #[serde(default)]
    pub server: ServerTlsConfig,
    #[serde(default = "default_cert_dir")]
    pub cert_dir: String,
    #[serde(default = "default_true")]
    pub hot_reload: bool,
    #[serde(default = "default_reload_interval")]
    pub reload_interval_secs: u64,
}

fn default_cert_dir() -> String {
    "./certs".to_string()
}

fn default_reload_interval_secs() -> u64 {
    3600
}

impl Default for TlsConfig {
    fn default() -> Self {
        Self {
            client: ClientTlsConfig::default(),
            server: ServerTlsConfig::default(),
            cert_dir: "./certs".to_string(),
            hot_reload: true,
            reload_interval_secs: 3600,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct InstanceConfig {
    pub name: String,
    pub listen_addr: String,
    pub target_addr: Option<String>,
    pub target_socket: Option<String>,
    #[serde(default)]
    pub compression_enabled: Option<bool>,
    #[serde(default)]
    pub min_compress_size: Option<usize>,
    #[serde(default)]
    pub compression_level: Option<i32>,
    #[serde(default)]
    pub adaptive_mode: Option<bool>,
    #[serde(default)]
    pub tls: Option<TlsConfig>,
    #[serde(default)]
    pub compress_columns: Option<Vec<CompressColumnConfig>>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DefaultConfig {
    #[serde(default = "default_true")]
    pub compression_enabled: bool,
    #[serde(default = "default_min_size")]
    pub min_compress_size: usize,
    #[serde(default = "default_level")]
    pub compression_level: i32,
    #[serde(default = "default_true")]
    pub adaptive_mode: bool,
    #[serde(default)]
    pub tls: TlsConfig,
}

fn default_true() -> bool {
    true
}
fn default_min_size() -> usize {
    4096
}
fn default_level() -> i32 {
    1
}

impl Default for DefaultConfig {
    fn default() -> Self {
        Self {
            compression_enabled: true,
            min_compress_size: 4096,
            compression_level: 1,
            adaptive_mode: true,
            tls: TlsConfig::default(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GlobalConfig {
    #[serde(default = "default_log_level")]
    pub log_level: String,
}

fn default_log_level() -> String {
    "info".to_string()
}

impl Default for GlobalConfig {
    fn default() -> Self {
        Self {
            log_level: "info".to_string(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    #[serde(default)]
    pub global: GlobalConfig,
    #[serde(default)]
    pub default: DefaultConfig,
    #[serde(default)]
    pub instance: Vec<InstanceConfig>,
}

impl Config {
    pub fn from_file(path: &Path) -> Result<Self, String> {
        if !path.exists() {
            return Err(format!("配置文件不存在: {}", path.display()));
        }
        let content = fs::read_to_string(path).map_err(|e| format!("读取配置文件失败: {}", e))?;
        let config: Config = toml::from_str(&content).map_err(|e| format!("解析配置文件失败: {}", e))?;
        config.validate()?;
        Ok(config)
    }

    pub fn validate(&self) -> Result<(), String> {
        if self.instance.is_empty() {
            return Err("配置文件中至少需要配置一个instance".to_string());
        }
        for (i, inst) in self.instance.iter().enumerate() {
            if inst.name.is_empty() {
                return Err(format!("第{}个instance的name不能为空", i + 1));
            }
            if inst.listen_addr.is_empty() {
                return Err(format!("instance '{}' 的listen_addr不能为空", inst.name));
            }
            if inst.target_addr.is_none() && inst.target_socket.is_none() {
                return Err(format!(
                    "instance '{}' 必须配置target_addr或target_socket",
                    inst.name
                ));
            }
            if let Some(min_size) = inst.min_compress_size {
                if min_size > 0 && min_size < 64 {
                    return Err(format!(
                        "instance '{}' 的min_compress_size不能小于64字节",
                        inst.name
                    ));
                }
            }
            if let Some(tls) = &inst.tls {
                if tls.client.enabled {
                    if tls.client.cert_file.is_none() || tls.client.key_file.is_none() {
                        return Err(format!(
                            "instance '{}' 启用客户端TLS时必须配置cert_file和key_file",
                            inst.name
                        ));
                    }
                }
                if tls.server.enabled && tls.server.verify_server_cert {
                    if tls.server.ca_file.is_none() {
                        return Err(format!(
                            "instance '{}' 启用服务器TLS验证时必须配置ca_file",
                            inst.name
                        ));
                    }
                }
            }
        }
        Ok(())
    }

    pub fn get_instance(&self, name: &str) -> Option<&InstanceConfig> {
        self.instance.iter().find(|i| i.name == name)
    }

    pub fn is_compression_enabled(&self, inst: &InstanceConfig) -> bool {
        inst.compression_enabled
            .unwrap_or(self.default.compression_enabled)
    }

    pub fn get_min_compress_size(&self, inst: &InstanceConfig) -> usize {
        inst.min_compress_size
            .unwrap_or(self.default.min_compress_size)
    }

    pub fn get_compression_level(&self, inst: &InstanceConfig) -> i32 {
        inst.compression_level
            .unwrap_or(self.default.compression_level)
    }

    pub fn is_adaptive_mode_enabled(&self, inst: &InstanceConfig) -> bool {
        inst.adaptive_mode
            .unwrap_or(self.default.adaptive_mode)
    }

    pub fn get_client_tls_config(&self, inst: &InstanceConfig) -> ClientTlsConfig {
        inst.tls
            .as_ref()
            .map(|t| tls.client.clone())
            .unwrap_or(self.default.tls.client.clone())
    }

    pub fn get_server_tls_config(&self, inst: &InstanceConfig) -> ServerTlsConfig {
        inst.tls
            .as_ref()
            .map(|t| tls.server.clone())
            .unwrap_or(self.default.tls.server.clone())
    }

    pub fn get_tls_config(&self, inst: &InstanceConfig) -> TlsConfig {
        inst.tls
            .clone()
            .unwrap_or(self.default.tls.clone())
    }
}
