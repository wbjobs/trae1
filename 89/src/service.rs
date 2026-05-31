use std::collections::HashMap;
use std::sync::Arc;
use std::time::Instant;

use arrow::datatypes::SchemaRef;
use arrow::ipc::writer::{IpcWriteOptions, StreamWriter};
use arrow_ipc::CompressionCodec;
use async_stream::try_stream;
use datafusion::datasource::MemTable;
use datafusion::prelude::*;
use futures::{Stream, StreamExt};
use tonic::{Request, Response, Status, Streaming};

use crate::proto::columnar::gateway::columnar_gateway_server::ColumnarGatewayServer;
use crate::proto::columnar::gateway::{
    ColumnarGateway, DeleteRequest, DeleteResponse,
    DatasetInfo, DownloadRequest, DownloadResponse, ListDatasetsRequest,
    ListDatasetsResponse, ListVersionsRequest, QueryRequest, QueryResponse,
    SchemaOptimization, UploadRequest, UploadResponse, VersionList,
    // 联合查询相关
    DatasourceConfig, DatasourceType,
    RegisterDatasourceRequest, RegisterDatasourceResponse,
    DropDatasourceRequest, DropDatasourceResponse,
    ListDatasourcesRequest, ListDatasourcesResponse,
    FederatedQueryRequest, FederatedQueryResponse,
    ExplainRequest, ExplainResponse,
    SlowQueryLogRequest, SlowQueryLogResponse, QueryStats,
};
use crate::datasource::{create_datasource, DatasourceRegistry};
use crate::federated_engine::FederatedQueryEngine;
use crate::query_log::QueryLogger;
use crate::schema_optimizer::{OptimizerConfig, optimize as optimize_schema};
use crate::storage::{ArrowStorage, CompressionCodecRef};

pub struct ColumnarGatewayService {
    store: Arc<ArrowStorage>,
    optimizer_config: OptimizerConfig,
    grpc_max_message_size: usize,
    datasource_registry: DatasourceRegistry,
    federated_engine: Option<Arc<FederatedQueryEngine>>,
    query_logger: Option<Arc<QueryLogger>>,
}

impl ColumnarGatewayService {
    pub fn new(
        store: ArrowStorage,
        optimizer_config: OptimizerConfig,
        grpc_max_message_size: usize,
        datasource_registry: DatasourceRegistry,
        federated_engine: Option<Arc<FederatedQueryEngine>>,
        query_logger: Option<Arc<QueryLogger>>,
    ) -> Self {
        Self {
            store: Arc::new(store),
            optimizer_config,
            grpc_max_message_size,
            datasource_registry,
            federated_engine,
            query_logger,
        }
    }

    pub fn into_grpc(self) -> ColumnarGatewayServer<Self> {
        ColumnarGatewayServer::new(self)
    }

    fn compression(&self) -> &CompressionCodecRef {
        self.store.compression()
    }

    fn require_federated_engine(&self) -> Result<&Arc<FederatedQueryEngine>, Status> {
        self.federated_engine.as_ref().ok_or_else(|| {
            Status::failed_precondition(
                "Federated query is disabled. Enable with --enable-federated-query"
            )
        })
    }
}

/// Helper: build IpcWriteOptions with the configured compression codec.
fn ipc_write_options(compression: &CompressionCodecRef) -> IpcWriteOptions {
    match compression {
        Some(CompressionCodec::LZ4_FRAME) => IpcWriteOptions::default()
            .try_with_compression(Some(CompressionCodec::LZ4_FRAME))
            .unwrap_or_else(|_| IpcWriteOptions::default()),
        Some(CompressionCodec::ZSTD) => IpcWriteOptions::default()
            .try_with_compression(Some(CompressionCodec::ZSTD))
            .unwrap_or_else(|_| IpcWriteOptions::default()),
        None => IpcWriteOptions::default(),
    }
}

#[tonic::async_trait]
impl ColumnarGateway for ColumnarGatewayService {
    async fn upload(
        &self,
        request: Request<Streaming<UploadRequest>>,
    ) -> Result<Response<UploadResponse>, Status> {
        let mut stream = request.into_inner();

        // 1. 读取首条消息确定 dataset_name
        let first = stream
            .next()
            .await
            .ok_or_else(|| Status::invalid_argument("empty upload stream"))??;
        let name = first.dataset_name;
        if name.is_empty() {
            return Err(Status::invalid_argument("missing dataset_name"));
        }

        // 2. 预分配版本号，创建 tmp 文件写入句柄
        let (version, tmp_path, final_path) = self
            .store
            .reserve_version(&name)
            .map_err(|e| Status::internal(format!("reserve: {e}")))?;

        // 3. 异步流式落盘：后台任务持续接收 bytes 写入 tmp 文件
        let (tx, mut rx) = tokio::sync::mpsc::unbounded_channel::<Vec<u8>>();
        let tmp_path_clone = tmp_path.clone();
        let writer = tokio::task::spawn(async move {
            use tokio::io::AsyncWriteExt;
            let mut f = tokio::io::BufWriter::new(tokio::fs::File::create(&tmp_path_clone).await?);
            let mut total: i64 = 0;
            while let Some(chunk) = rx.recv().await {
                total += chunk.len() as i64;
                f.write_all(&chunk).await?;
            }
            f.flush().await?;
            f.shutdown().await?;
            Ok::<_, std::io::Error>(total)
        });

        // 4. 把首条消息及后续消息的 arrow_ipc 喂给 writer
        if !first.arrow_ipc.is_empty() {
            let _ = tx.send(first.arrow_ipc);
        }
        while let Some(msg) = stream.next().await {
            let msg = msg?;
            if !msg.arrow_ipc.is_empty() {
                let _ = tx.send(msg.arrow_ipc);
            }
        }
        drop(tx);

        // 5. 等待 writer 完成，然后读取、验证、优化 schema
        let total_bytes = writer
            .await
            .map_err(|e| Status::internal(format!("writer join: {e}")))?
            .map_err(|e| Status::internal(format!("writer io: {e}")))?;

        let tmp_path_clone = tmp_path.clone();
        let compression = self.compression().clone();
        let optimizer_cfg = self.optimizer_config.clone();

        // 读取原始数据（可能是客户端发送的 Arrow IPC File 格式），
        // 应用 schema 优化（字典编码），然后写回压缩后的 IPC File。
        let (row_count, schema_json, optimized_bytes, suggestions) = tokio::task::spawn_blocking(
            move || -> Result<(i64, String, i64, Option<SchemaOptimization>), Status> {
                use std::io::BufWriter;

                let f = std::fs::File::open(&tmp_path_clone)
                    .map_err(|e| Status::internal(format!("open: {e}")))?;
                let reader = arrow::ipc::reader::FileReader::try_new(std::io::BufReader::new(f), None)
                    .map_err(|e| Status::internal(format!("Arrow FileReader: {e}")))?;
                let _orig_schema = reader.schema();

                let mut batches: Vec<arrow::array::RecordBatch> = Vec::new();
                for r in reader {
                    let b = r.map_err(|e| Status::internal(format!("batch: {e}")))?;
                    batches.push(b);
                }

                let mut rows: i64 = batches.iter().map(|b| b.num_rows() as i64).sum();

                // 6. 应用 schema 优化（字典编码等）
                let optimized = optimize_schema(batches, &optimizer_cfg)
                    .map_err(|e| Status::internal(format!("schema optimization: {e}")))?;

                let suggestions = optimized.suggestions;
                let schema_json = serde_json::to_string(optimized.schema.as_ref())
                    .map_err(|e| Status::internal(format!("schema json: {e}")))?;

                // 7. 将优化后的 batches 写回 tmp_path（使用 LZ4 压缩）
                let mut buf = Vec::with_capacity((total_bytes * 2) as usize);
                let mut writer = arrow::ipc::writer::FileWriter::try_new_with_options(
                    &mut buf,
                    &optimized.schema,
                    ipc_write_options(&compression),
                ).map_err(|e| Status::internal(format!("FileWriter: {e}")))?;

                for batch in &optimized.batches {
                    writer
                        .write(batch)
                        .map_err(|e| Status::internal(format!("write: {e}")))?;
                }
                writer
                    .finish()
                    .map_err(|e| Status::internal(format!("finish: {e}")))?;
                drop(writer);

                let optimized_bytes = buf.len() as i64;

                // 同步落盘优化后的压缩数据
                let file = std::fs::File::create(&tmp_path_clone)
                    .map_err(|e| Status::internal(format!("create: {e}")))?;
                let mut bw = BufWriter::new(file);
                use std::io::Write;
                bw.write_all(&buf)
                    .map_err(|e| Status::internal(format!("write_all: {e}")))?;
                bw.flush()
                    .map_err(|e| Status::internal(format!("flush: {e}")))?;

                // 同步读取校验
                {
                    let f = std::fs::File::open(&tmp_path_clone)
                        .map_err(|e| Status::internal(format!("verify open: {e}")))?;
                    let reader = arrow::ipc::reader::FileReader::try_new(std::io::BufReader::new(f), None)
                        .map_err(|e| Status::internal(format!("verify reader: {e}")))?;
                    let verifiable_schema = reader.schema();
                    assert_eq!(verifiable_schema, optimized.schema,
                        "schema mismatch after optimization!");
                    rows = 0;
                    for r in reader {
                        let b = r.map_err(|e| Status::internal(format!("verify batch: {e}")))?;
                        rows += b.num_rows() as i64;
                    }
                }

                Ok((rows, schema_json, optimized_bytes, suggestions))
            },
        )
        .await
        .map_err(|e| Status::internal(format!("join: {e}")))??;

        // 8. 原子提交
        let info = self
            .store
            .commit_version(
                &name,
                version,
                tmp_path,
                final_path,
                row_count,
                optimized_bytes,
                schema_json,
            )
            .await
            .map_err(|e| Status::internal(format!("commit: {e}")))?;

        Ok(Response::new(UploadResponse {
            info: Some(info),
            optimization: suggestions,
        }))
    }

    type DownloadStream =
        tokio_stream::wrappers::UnboundedReceiverStream<Result<DownloadResponse, Status>>;

    async fn download(
        &self,
        request: Request<DownloadRequest>,
    ) -> Result<Response<Self::DownloadStream>, Status> {
        let req = request.into_inner();
        let batches = self
            .store
            .read_record_batches(&req.dataset_name, req.version)
            .await
            .map_err(|e| Status::internal(format!("read: {e}")))?;

        let (tx, rx) = tokio::sync::mpsc::unbounded_channel();
        let compression = self.compression().clone();

        std::thread::spawn(move || {
            let schema: SchemaRef = if let Some(b) = batches.first() {
                Arc::new(b.schema())
            } else {
                Arc::new(arrow::datatypes::Schema::empty())
            };
            for batch in batches {
                let mut buf = Vec::with_capacity(16 * 1024);
                let mut w = match StreamWriter::try_new_with_options(
                    &mut buf,
                    &schema,
                    ipc_write_options(&compression),
                ) {
                    Ok(w) => w,
                    Err(e) => {
                        let _ = tx.send(Err(Status::internal(format!("writer: {e}"))));
                        return;
                    }
                };
                if let Err(e) = w.write(&batch) {
                    let _ = tx.send(Err(Status::internal(format!("write: {e}"))));
                    return;
                }
                if let Err(e) = w.finish() {
                    let _ = tx.send(Err(Status::internal(format!("finish: {e}"))));
                    return;
                }
                drop(w);
                if tx
                    .send(Ok(DownloadResponse {
                        arrow_ipc: buf.into(),
                    }))
                    .is_err()
                {
                    return;
                }
            }
        });

        let stream = tokio_stream::wrappers::UnboundedReceiverStream::new(rx);
        Ok(Response::new(stream))
    }

    type QueryStream =
        tokio_stream::wrappers::UnboundedReceiverStream<Result<QueryResponse, Status>>;

    async fn query(
        &self,
        request: Request<QueryRequest>,
    ) -> Result<Response<Self::QueryStream>, Status> {
        let req = request.into_inner();
        if req.sql.trim().is_empty() {
            return Err(Status::invalid_argument("sql is required"));
        }

        let batches = self
            .store
            .read_record_batches(&req.dataset_name, req.version)
            .await
            .map_err(|e| Status::internal(format!("read: {e}")))?;
        if batches.is_empty() {
            return Err(Status::internal("empty dataset"));
        }

        let schema: SchemaRef = Arc::new(batches[0].schema());
        let mem = MemTable::try_new(schema, vec![batches])
            .map_err(|e| Status::internal(format!("memtable: {e}")))?;

        let ctx = SessionContext::new();
        ctx.register_table("t", Arc::new(mem))
            .map_err(|e| Status::internal(format!("register: {e}")))?;

        let sql = normalize_sql(&req.sql);
        tracing::debug!("executing SQL: {sql}");

        let df = ctx
            .sql(&sql)
            .await
            .map_err(|e| Status::internal(format!("sql parse/plan: {e}")))?;

        let batches = df
            .collect()
            .await
            .map_err(|e| Status::internal(format!("exec: {e}")))?;

        let (tx, rx) = tokio::sync::mpsc::unbounded_channel();
        let compression = self.compression().clone();
        std::thread::spawn(move || {
            let out_schema: SchemaRef = if let Some(b) = batches.first() {
                Arc::new(b.schema())
            } else {
                Arc::new(arrow::datatypes::Schema::empty())
            };
            for batch in batches {
                let mut buf = Vec::with_capacity(16 * 1024);
                let mut w = match StreamWriter::try_new_with_options(
                    &mut buf,
                    &out_schema,
                    ipc_write_options(&compression),
                ) {
                    Ok(w) => w,
                    Err(e) => {
                        let _ = tx.send(Err(Status::internal(format!("writer: {e}"))));
                        return;
                    }
                };
                if let Err(e) = w.write(&batch) {
                    let _ = tx.send(Err(Status::internal(format!("write: {e}"))));
                    return;
                }
                if let Err(e) = w.finish() {
                    let _ = tx.send(Err(Status::internal(format!("finish: {e}"))));
                    return;
                }
                drop(w);
                if tx
                    .send(Ok(QueryResponse {
                        arrow_ipc: buf.into(),
                    }))
                    .is_err()
                {
                    return;
                }
            }
        });

        let stream = tokio_stream::wrappers::UnboundedReceiverStream::new(rx);
        Ok(Response::new(stream))
    }

    async fn list_datasets(
        &self,
        _request: Request<ListDatasetsRequest>,
    ) -> Result<Response<ListDatasetsResponse>, Status> {
        let names = self.store.list_datasets();
        Ok(Response::new(ListDatasetsResponse { names }))
    }

    async fn list_versions(
        &self,
        request: Request<ListVersionsRequest>,
    ) -> Result<Response<VersionList>, Status> {
        let name = request.into_inner().dataset_name;
        let versions = self
            .store
            .list_versions(&name)
            .map_err(|e| Status::internal(format!("list_versions: {e}")))?;
        Ok(Response::new(VersionList { versions }))
    }

    async fn delete(
        &self,
        request: Request<DeleteRequest>,
    ) -> Result<Response<DeleteResponse>, Status> {
        let req = request.into_inner();
        let ok = self
            .store
            .delete(&req.dataset_name, req.version)
            .await
            .map_err(|e| Status::internal(format!("delete: {e}")))?;
        Ok(Response::new(DeleteResponse { ok }))
    }

    // -------------------------------------------------------------------------
    // 联合查询：数据源管理
    // -------------------------------------------------------------------------

    async fn register_datasource(
        &self,
        request: Request<RegisterDatasourceRequest>,
    ) -> Result<Response<RegisterDatasourceResponse>, Status> {
        let engine = self.require_federated_engine()?;
        let config = request
            .into_inner()
            .config
            .ok_or_else(|| Status::invalid_argument("missing datasource config"))?;

        if config.name.is_empty() {
            return Err(Status::invalid_argument("datasource name is required"));
        }

        if config.connection_string.is_empty() && config.r#type() != DatasourceType::DatasourceLocalParquet {
            return Err(Status::invalid_argument("connection string is required"));
        }

        let ds = create_datasource(config.clone())
            .await
            .map_err(|e| Status::internal(format!("create datasource: {e}")))?;

        self.datasource_registry.register(ds);

        tracing::info!(
            name = config.name,
            datasource_type = ?config.r#type(),
            "datasource registered"
        );

        Ok(Response::new(RegisterDatasourceResponse {
            ok: true,
            message: format!("Datasource '{}' registered successfully", config.name),
        }))
    }

    async fn drop_datasource(
        &self,
        request: Request<DropDatasourceRequest>,
    ) -> Result<Response<DropDatasourceResponse>, Status> {
        let _ = self.require_federated_engine()?;
        let name = request.into_inner().name;

        let ok = self.datasource_registry.drop(&name);
        if ok {
            tracing::info!(name, "datasource dropped");
        }

        Ok(Response::new(DropDatasourceResponse { ok }))
    }

    async fn list_datasources(
        &self,
        _request: Request<ListDatasourcesRequest>,
    ) -> Result<Response<ListDatasourcesResponse>, Status> {
        let _ = self.require_federated_engine()?;
        let datasources = self.datasource_registry.list();
        Ok(Response::new(ListDatasourcesResponse { datasources }))
    }

    // -------------------------------------------------------------------------
    // 联合查询：查询执行
    // -------------------------------------------------------------------------

    type FederatedQueryStream =
        tokio_stream::wrappers::UnboundedReceiverStream<Result<FederatedQueryResponse, Status>>;

    async fn federated_query(
        &self,
        request: Request<FederatedQueryRequest>,
    ) -> Result<Response<Self::FederatedQueryStream>, Status> {
        let engine = self.require_federated_engine()?.clone();
        let req = request.into_inner();

        if req.sql.trim().is_empty() {
            return Err(Status::invalid_argument("sql is required"));
        }

        let (tx, rx) = tokio::sync::mpsc::unbounded_channel();
        let compression = self.compression().clone();

        tokio::spawn(async move {
            let start = Instant::now();

            let result = engine
                .execute(
                    &req.sql,
                    if req.timeout_seconds > 0 {
                        Some(req.timeout_seconds as u64)
                    } else {
                        None
                    },
                    req.include_explain,
                )
                .await;

            match result {
                Ok(result) => {
                    let elapsed_ms = start.elapsed().as_millis() as i64;

                    if req.include_explain {
                        if let Some(plan) = &result.execution_plan {
                            let plan_json = plan.to_json();
                            let _ = tx.send(Ok(FederatedQueryResponse {
                                arrow_ipc: Vec::new(),
                                execution_plan_json: plan_json,
                                elapsed_ms,
                            }));
                        }
                    }

                    let out_schema: SchemaRef = if let Some(b) = result.batches.first() {
                        Arc::new(b.schema())
                    } else {
                        Arc::new(arrow::datatypes::Schema::empty())
                    };

                    for batch in &result.batches {
                        let mut buf = Vec::with_capacity(16 * 1024);
                        let mut w = match StreamWriter::try_new_with_options(
                            &mut buf,
                            &out_schema,
                            ipc_write_options(&compression),
                        ) {
                            Ok(w) => w,
                            Err(e) => {
                                let _ = tx.send(Err(Status::internal(format!("writer: {e}"))));
                                return;
                            }
                        };
                        if let Err(e) = w.write(batch) {
                            let _ = tx.send(Err(Status::internal(format!("write: {e}"))));
                            return;
                        }
                        if let Err(e) = w.finish() {
                            let _ = tx.send(Err(Status::internal(format!("finish: {e}"))));
                            return;
                        }
                        drop(w);

                        if tx
                            .send(Ok(FederatedQueryResponse {
                                arrow_ipc: buf.into(),
                                execution_plan_json: String::new(),
                                elapsed_ms,
                            }))
                            .is_err()
                        {
                            return;
                        }
                    }

                    tracing::debug!(
                        query_id = result.query_id,
                        elapsed_ms,
                        rows = result.rows_returned,
                        "federated query completed"
                    );
                }
                Err(e) => {
                    let err_msg = match e {
                        crate::federated_engine::FederatedQueryError::Timeout(t) => {
                            format!("Query timeout after {} seconds", t)
                        }
                        _ => format!("Query error: {}", e),
                    };
                    let _ = tx.send(Err(Status::internal(err_msg)));
                }
            }
        });

        let stream = tokio_stream::wrappers::UnboundedReceiverStream::new(rx);
        Ok(Response::new(stream))
    }

    // -------------------------------------------------------------------------
    // 联合查询：执行计划
    // -------------------------------------------------------------------------

    async fn explain(
        &self,
        request: Request<ExplainRequest>,
    ) -> Result<Response<ExplainResponse>, Status> {
        let engine = self.require_federated_engine()?;
        let req = request.into_inner();

        if req.sql.trim().is_empty() {
            return Err(Status::invalid_argument("sql is required"));
        }

        let plan = engine
            .explain(&req.sql, req.verbose)
            .map_err(|e| Status::internal(format!("explain: {e}")))?;

        Ok(Response::new(ExplainResponse {
            plan_json: plan.to_json(),
            plan_text: plan.to_text(),
        }))
    }

    // -------------------------------------------------------------------------
    // 联合查询：慢查询日志
    // -------------------------------------------------------------------------

    async fn slow_query_log(
        &self,
        request: Request<SlowQueryLogRequest>,
    ) -> Result<Response<SlowQueryLogResponse>, Status> {
        let logger = self
            .query_logger
            .as_ref()
            .ok_or_else(|| Status::failed_precondition("Query logging is disabled"))?;

        let req = request.into_inner();
        let limit = if req.limit <= 0 { 100 } else { req.limit as usize };
        let entries = logger.get_slow_queries(limit);

        let queries: Vec<QueryStats> = entries.iter().map(|e| e.to_proto()).collect();

        Ok(Response::new(SlowQueryLogResponse { queries }))
    }
}

/// DataFusion 要求 FROM 子句；如果用户写 `SELECT col1, col2 WHERE col3>10`，
/// 我们在末尾自动追加 `FROM t`，同时保留 WHERE/ORDER BY/LIMIT 等。
fn normalize_sql(sql: &str) -> String {
    let trimmed = sql.trim();
    let lower = trimmed.to_ascii_lowercase();

    if lower.contains("from ") {
        return trimmed.to_string();
    }

    let sel_idx = match lower.find("select") {
        Some(i) => i,
        None => return trimmed.to_string(),
    };

    let after_select = &lower[sel_idx + 6..];
    let keywords = ["where", "group", "having", "order", "limit", "offset"];
    let mut cut = trimmed.len();
    for kw in keywords {
        if let Some(i) = after_select.find(kw) {
            cut = (sel_idx + 6 + i).min(cut);
        }
    }
    let (head, tail) = trimmed.split_at(cut);
    format!("{} FROM t{}", head.trim_end(), tail)
}
