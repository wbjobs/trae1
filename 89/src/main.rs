mod proto {
    pub mod columnar {
        pub mod gateway {
            tonic::include_proto!("columnar.gateway");
        }
    }
}

mod storage;
mod service;
mod schema_optimizer;
mod datasource;
mod federated_optimizer;
mod federated_engine;
mod join_executor;
mod query_log;

use clap::Parser;
use std::sync::Arc;
use tracing_subscriber::{layer::SubscriberExt, util::SubscriberInitExt};

#[derive(Parser, Debug)]
#[command(name = "columnar-gateway", about = "Arrow columnar data gateway")]
struct Args {
    #[arg(long, default_value = "[::1]:50051")]
    addr: String,

    #[arg(long, default_value = "./data")]
    data_dir: String,

    /// 每个数据集保留的最大历史版本数（含当前）
    #[arg(long, default_value_t = 10)]
    max_versions: usize,

    /// 单个 record batch 的建议行数，用于上传分片/下载分片
    #[arg(long, default_value_t = 65536)]
    batch_rows: usize,

    /// 自动检测低基数字符串列并转换为字典编码（唯一值 < 总行数的 10%）
    #[arg(long, default_value_t = false)]
    optimize_schema: bool,

    /// 字典编码的基数阈值（默认 0.10，即 10%）
    #[arg(long, default_value_t = 0.10)]
    dict_cardinality_threshold: f64,

    /// Arrow IPC 压缩方式：none | lz4 (默认 lz4)
    #[arg(long, default_value = "lz4")]
    compression: String,

    /// gRPC 消息最大大小（字节），默认 2GB
    #[arg(long, default_value_t = 2 * 1024 * 1024 * 1024)]
    grpc_max_message_size: usize,

    // ========== 联合查询 (Federated Query) ==========

    /// 是否启用联合查询功能
    #[arg(long, default_value_t = true)]
    enable_federated_query: bool,

    /// 默认查询超时时间（秒），默认 60s
    #[arg(long, default_value_t = 60)]
    query_timeout_sec: u64,

    /// 慢查询阈值（毫秒），超过此时间的查询会被记录到慢查询日志，默认 5000ms
    #[arg(long, default_value_t = 5000)]
    slow_query_threshold_ms: u64,

    /// Hash Join 阈值（行数），小于此值使用 Hash Join
    #[arg(long, default_value_t = 100_000)]
    hash_join_threshold_rows: usize,

    /// Sort Merge Join 阈值（行数），大于此值使用 Sort Merge Join
    #[arg(long, default_value_t = 1_000_000)]
    sort_merge_join_threshold_rows: usize,

    /// 是否启用谓词下推优化
    #[arg(long, default_value_t = true)]
    enable_predicate_pushdown: bool,

    /// 是否启用列裁剪优化
    #[arg(long, default_value_t = true)]
    enable_column_pruning: bool,

    /// 是否启用 Join 重排序优化
    #[arg(long, default_value_t = true)]
    enable_join_reordering: bool,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    tracing_subscriber::registry()
        .with(tracing_subscriber::EnvFilter::try_from_default_env()
            .unwrap_or_else(|_| "info".into()))
        .with(tracing_subscriber::fmt::layer())
        .init();

    let args = Args::parse();

    let compression_codec = match args.compression.to_ascii_lowercase().as_str() {
        "lz4" => Some(arrow_ipc::CompressionCodec::LZ4_FRAME),
        "zstd" => Some(arrow_ipc::CompressionCodec::ZSTD),
        "none" | _ => None,
    };

    if let Some(c) = &compression_codec {
        tracing::info!("Arrow IPC compression: {:?}", c);
    } else {
        tracing::info!("Arrow IPC compression: disabled");
    }

    let optimizer_config = schema_optimizer::OptimizerConfig {
        apply_dictionary: args.optimize_schema,
        generate_suggestions: true,
        cardinality_threshold: args.dict_cardinality_threshold,
        compression_codec: compression_codec.clone(),
    };

    if args.optimize_schema {
        tracing::info!(
            "Schema optimization: enabled (cardinality threshold: {:.1}%)",
            args.dict_cardinality_threshold * 100.0
        );
    }

    let store = storage::ArrowStorage::new(
        &args.data_dir,
        args.max_versions,
        args.batch_rows,
        compression_codec,
    )
    .await?;

    let optimizer_config = schema_optimizer::OptimizerConfig {
        apply_dictionary: args.optimize_schema,
        generate_suggestions: true,
        cardinality_threshold: args.dict_cardinality_threshold,
        compression_codec: compression_codec.clone(),
    };

    if args.optimize_schema {
        tracing::info!(
            "Schema optimization: enabled (cardinality threshold: {:.1}%)",
            args.dict_cardinality_threshold * 100.0
        );
    }

    // ========== 联合查询引擎初始化 ==========
    let federated_config = if args.enable_federated_query {
        Some(federated_optimizer::OptimizerConfig {
            enable_predicate_pushdown: args.enable_predicate_pushdown,
            enable_column_pruning: args.enable_column_pruning,
            enable_join_reordering: args.enable_join_reordering,
            enable_aggregate_pushdown: true,
            hash_join_threshold_rows: args.hash_join_threshold_rows,
            sort_merge_join_threshold_rows: args.sort_merge_join_threshold_rows,
            default_query_timeout_sec: args.query_timeout_sec,
            slow_query_threshold_ms: args.slow_query_threshold_ms,
        })
    } else {
        None
    };

    let datasource_registry = datasource::DatasourceRegistry::new();
    let query_logger = if args.enable_federated_query {
        let logger = Arc::new(query_log::QueryLogger::new(
            federated_config.clone().unwrap(),
            1000,
        ));
        Some(logger)
    } else {
        None
    };

    let federated_engine = if args.enable_federated_query {
        let engine = Arc::new(federated_engine::FederatedQueryEngine::new(
            federated_config.clone().unwrap(),
            datasource_registry.clone(),
            query_logger.clone().unwrap(),
        ));
        Some(engine)
    } else {
        None
    };

    if args.enable_federated_query {
        tracing::info!(
            "Federated query: enabled (timeout={}s, slow_threshold={}ms)",
            args.query_timeout_sec,
            args.slow_query_threshold_ms
        );
        tracing::info!(
            "  Join strategy: hash_join < {} rows, sort_merge_join > {} rows",
            args.hash_join_threshold_rows,
            args.sort_merge_join_threshold_rows
        );
        tracing::info!(
            "  Optimizations: predicate_pushdown={}, column_pruning={}, join_reordering={}",
            args.enable_predicate_pushdown,
            args.enable_column_pruning,
            args.enable_join_reordering
        );
    }

    let svc = service::ColumnarGatewayService::new(
        store,
        optimizer_config,
        args.grpc_max_message_size,
        datasource_registry,
        federated_engine,
        query_logger,
    );
    let addr: std::net::SocketAddr = args.addr.parse()?;

    tracing::info!(
        "columnar-gateway listening on {addr}, data_dir={}, optimize_schema={}",
        args.data_dir,
        args.optimize_schema
    );

    // Configure gRPC: enable LZ4 / gzip compression and set message size limits
    let svc_with_limits = svc.into_grpc()
        .max_encoding_message_size(args.grpc_max_message_size)
        .max_decoding_message_size(args.grpc_max_message_size)
        .accept_compressed(tonic::codec::CompressionEncoding::Gzip)
        .send_compressed(tonic::codec::CompressionEncoding::Gzip);

    tonic::transport::Server::builder()
        .add_service(svc_with_limits)
        .serve_with_shutdown(addr, async {
            let _ = tokio::signal::ctrl_c().await;
            tracing::info!("shutting down");
        })
        .await?;

    Ok(())
}
