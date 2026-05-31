mod compression;
mod config;
mod protocol;
mod proxy;
mod stats;
mod tls;

use clap::{Parser, Subcommand};
use config::Config;
use proxy::ProxyServer;
use stats::{format_stats_table, SharedStats, StatsStore};
use std::path::PathBuf;
use std::sync::Arc;
use tracing_subscriber::{layer::SubscriberExt, util::SubscriberInitExt};

#[derive(Parser)]
#[command(name = "pg-proxy")]
#[command(author, version, about = "PostgreSQL transparent compression proxy", long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,

    #[arg(short, long, default_value = "pg-proxy.toml", help = "配置文件路径")]
    config: PathBuf,
}

#[derive(Subcommand)]
enum Commands {
    #[command(about = "启动代理服务器")]
    Start {
        #[arg(short, long, help = "配置文件路径")]
        config: Option<PathBuf>,
    },

    #[command(about = "显示统计信息")]
    Stats {
        #[arg(short, long, help = "指定实例名称，不指定则显示所有实例")]
        instance: Option<String>,

        #[arg(short, long, help = "JSON格式输出")]
        json: bool,
    },

    #[command(about = "验证配置文件")]
    Validate {
        #[arg(short, long, help = "配置文件路径")]
        config: Option<PathBuf>,
    },

    #[command(about = "显示当前配置")]
    ShowConfig {
        #[arg(short, long, help = "配置文件路径")]
        config: Option<PathBuf>,
    },

    #[command(about = "生成自签名TLS证书")]
    GenCert {
        #[arg(short, long, help = "证书输出目录")]
        output: Option<PathBuf>,

        #[arg(short, long, help = "通用名称(CN)", default_value = "pg-proxy")]
        cn: String,

        #[arg(short = 'O', long, help = "组织名称", default_value = "PG Proxy")]
        organization: String,

        #[arg(short, long, help = "有效期(天)", default_value = "365")]
        days: u32,

        #[arg(long, help = "同时生成客户端证书")]
        client: bool,
    },
}

fn setup_logging(level: &str) {
    let log_level = match level.to_lowercase().as_str() {
        "trace" => tracing::Level::TRACE,
        "debug" => tracing::Level::DEBUG,
        "info" => tracing::Level::INFO,
        "warn" => tracing::Level::WARN,
        "error" => tracing::Level::ERROR,
        _ => tracing::Level::INFO,
    };

    tracing_subscriber::registry()
        .with(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| {
                    tracing_subscriber::EnvFilter::new(format!("pg_proxy={}", log_level))
                }),
        )
        .with(tracing_subscriber::fmt::layer())
        .init();
}

fn load_config(path: &PathBuf) -> Result<Arc<Config>, String> {
    let config = Config::from_file(path)?;
    Ok(Arc::new(config))
}

#[tokio::main]
async fn main() {
    let cli = Cli::parse();

    let config_path = match &cli.command {
        Commands::Start { config }
        | Commands::Validate { config }
        | Commands::ShowConfig { config } => config.clone().unwrap_or(cli.config.clone()),
        Commands::Stats { .. } | Commands::GenCert { .. } => cli.config.clone(),
    };

    match cli.command {
        Commands::Start { .. } => {
            match load_config(&config_path) {
                Ok(config) => {
                    setup_logging(&config.global.log_level);

                    let stats: SharedStats = Arc::new(StatsStore::new());

                    println!("=== PostgreSQL Proxy 启动 ===");
                    println!("配置文件: {}", config_path.display());
                    println!("实例数量: {}", config.instance.len());
                    println!();

                    for inst in &config.instance {
                        let tls_config = config.get_tls_config(inst);
                        println!(
                            "  [{}] {} -> {}",
                            inst.name,
                            inst.listen_addr,
                            inst.target_addr
                                .as_deref()
                                .or(inst.target_socket.as_deref())
                                .unwrap_or("unknown")
                        );
                        println!(
                            "    压缩: {} | 最小大小: {} bytes | 压缩级别: {} | 自适应: {}",
                            if config.is_compression_enabled(inst) {
                                "启用"
                            } else {
                                "禁用"
                            },
                            config.get_min_compress_size(inst),
                            config.get_compression_level(inst),
                            if config.is_adaptive_mode_enabled(inst) {
                                "启用"
                            } else {
                                "禁用"
                            }
                        );
                        println!(
                            "    TLS: 客户端={} 服务器={} 热加载={}",
                            if tls_config.client.enabled { "启用" } else { "禁用" },
                            if tls_config.server.enabled { "启用" } else { "禁用" },
                            if tls_config.hot_reload { "启用" } else { "禁用" }
                        );
                    }
                    println!();

                    let proxy = ProxyServer::new(config, stats);
                    if let Err(e) = proxy.start().await {
                        eprintln!("代理服务器错误: {}", e);
                        std::process::exit(1);
                    }
                }
                Err(e) => {
                    eprintln!("加载配置文件失败: {}", e);
                    std::process::exit(1);
                }
            }
        }

        Commands::Stats { instance, json } => {
            match load_config(&config_path) {
                Ok(config) => {
                    let stats: SharedStats = Arc::new(StatsStore::new());

                    for inst in &config.instance {
                        stats.register_instance(&inst.name);
                    }

                    match instance {
                        Some(name) => {
                            if let Some(inst_stats) = stats.get_stats(&name) {
                                if json {
                                    match serde_json::to_string_pretty(&inst_stats) {
                                        Ok(json) => println!("{}", json),
                                        Err(e) => eprintln!("序列化失败: {}", e),
                                    }
                                } else {
                                    println!("{}", format_stats_table(&[inst_stats]));
                                }
                            } else {
                                eprintln!("未找到实例: {}", name);
                            }
                        }
                        None => {
                            let all_stats = stats.get_all_stats();
                            if json {
                                match serde_json::to_string_pretty(&all_stats) {
                                    Ok(json) => println!("{}", json),
                                    Err(e) => eprintln!("序列化失败: {}", e),
                                }
                            } else {
                                println!("{}", format_stats_table(&all_stats));
                            }
                        }
                    }
                }
                Err(e) => {
                    eprintln!("加载配置文件失败: {}", e);
                    std::process::exit(1);
                }
            }
        }

        Commands::Validate { .. } => {
            match load_config(&config_path) {
                Ok(config) => {
                    println!("配置文件验证成功!");
                    println!("实例数量: {}", config.instance.len());
                    for inst in &config.instance {
                        let tls_config = config.get_tls_config(inst);
                        println!(
                            "  [{}] {} -> {} (TLS: client={}, server={})",
                            inst.name,
                            inst.listen_addr,
                            inst.target_addr
                                .as_deref()
                                .or(inst.target_socket.as_deref())
                                .unwrap_or("unknown"),
                            tls_config.client.enabled,
                            tls_config.server.enabled
                        );
                    }
                }
                Err(e) => {
                    eprintln!("配置文件验证失败: {}", e);
                    std::process::exit(1);
                }
            }
        }

        Commands::ShowConfig { .. } => {
            match load_config(&config_path) {
                Ok(config) => {
                    println!("=== 当前配置 ===");
                    match toml::to_string_pretty(&*config) {
                        Ok(toml_str) => println!("{}", toml_str),
                        Err(e) => eprintln!("序列化配置失败: {}", e),
                    }
                }
                Err(e) => {
                    eprintln!("加载配置文件失败: {}", e);
                    std::process::exit(1);
                }
            }
        }

        Commands::GenCert { output, cn, organization, days, client } => {
            let output_dir = output.unwrap_or_else(|| PathBuf::from("./certs"));
            
            println!("=== 生成TLS证书 ===");
            println!("输出目录: {}", output_dir.display());
            println!("通用名称: {}", cn);
            println!("组织名称: {}", organization);
            println!("有效期: {} 天", days);
            
            if client {
                match tls::generate_certificates(&output_dir, &cn, &organization, days) {
                    Ok(certs) => {
                        println!("\n证书生成成功!");
                        println!("  CA证书: {}", certs.ca_cert);
                        println!("  CA密钥: {}", certs.ca_key);
                        println!("  服务器证书: {}", certs.server_cert);
                        println!("  服务器密钥: {}", certs.server_key);
                        println!("  客户端证书: {}", certs.client_cert);
                        println!("  客户端密钥: {}", certs.client_key);
                        
                        println!("\n配置示例:");
                        println!("[tls]");
                        println!("client.enabled = true");
                        println!("client.cert_file = \"{}\"", certs.server_cert);
                        println!("client.key_file = \"{}\"", certs.server_key);
                        println!("client.ca_file = \"{}\"", certs.ca_cert);
                        println!("client.require_client_cert = true");
                        println!();
                        println!("[tls.server]");
                        println!("enabled = true");
                        println!("cert_file = \"{}\"", certs.client_cert);
                        println!("key_file = \"{}\"", certs.client_key);
                        println!("ca_file = \"{}\"", certs.ca_cert);
                        println!("verify_server_cert = true");
                    }
                    Err(e) => {
                        eprintln!("生成证书失败: {}", e);
                        std::process::exit(1);
                    }
                }
            } else {
                match tls::generate_self_signed_cert(&cn, &organization, days) {
                    Ok((cert_pem, key_pem)) => {
                        match tls::save_cert_and_key(&output_dir, &cert_pem, &key_pem, "server.crt", "server.key") {
                            Ok((cert_path, key_path)) => {
                                println!("\n证书生成成功!");
                                println!("  证书文件: {}", cert_path);
                                println!("  密钥文件: {}", key_path);
                                
                                println!("\n配置示例:");
                                println!("[tls]");
                                println!("client.enabled = true");
                                println!("client.cert_file = \"{}\"", cert_path);
                                println!("client.key_file = \"{}\"", key_path);
                            }
                            Err(e) => {
                                eprintln!("保存证书失败: {}", e);
                                std::process::exit(1);
                            }
                        }
                    }
                    Err(e) => {
                        eprintln!("生成证书失败: {}", e);
                        std::process::exit(1);
                    }
                }
            }
        }
    }
}
