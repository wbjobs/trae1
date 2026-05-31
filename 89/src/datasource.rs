use std::collections::HashMap;
use std::sync::Arc;

use arrow::datatypes::{DataType, Field, Schema, SchemaRef};
use arrow_array::{ArrayRef, Int64Array, RecordBatch, StringArray};
use async_trait::async_trait;
use datafusion::datasource::TableProvider;
use datafusion::execution::context::SessionState;
use datafusion::logical_expr::Expr;
use datafusion::prelude::*;
use serde::{Deserialize, Serialize};
use tracing::*;
use uuid::Uuid;

use crate::proto::{DatasourceConfig, DatasourceType};

#[derive(Debug, thiserror::Error)]
pub enum DatasourceError {
    #[error("Connection error: {0}")]
    Connection(String),
    #[error("Query error: {0}")]
    Query(String),
    #[error("Schema error: {0}")]
    Schema(String),
    #[error("Unsupported operation: {0}")]
    Unsupported(String),
    #[error("Invalid config: {0}")]
    InvalidConfig(String),
}

pub type Result<T> = std::result::Result<T, DatasourceError>;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PushdownInfo {
    pub can_push_predicates: bool,
    pub can_push_projection: bool,
    pub can_push_aggregation: bool,
    pub can_push_join: bool,
    pub pushed_predicates: Vec<String>,
    pub pushed_columns: Vec<String>,
}

impl Default for PushdownInfo {
    fn default() -> Self {
        Self {
            can_push_predicates: true,
            can_push_projection: true,
            can_push_aggregation: true,
            can_push_join: false,
            pushed_predicates: Vec::new(),
            pushed_columns: Vec::new(),
        }
    }
}

#[async_trait]
pub trait Datasource: Send + Sync {
    fn id(&self) -> &str;
    fn name(&self) -> &str;
    fn datasource_type(&self) -> DatasourceType;
    fn config(&self) -> &DatasourceConfig;

    async fn list_tables(&self) -> Result<Vec<String>>;

    async fn get_schema(&self, table: &str) -> Result<SchemaRef>;

    async fn execute_query(
        &self,
        table: &str,
        columns: &[String],
        predicates: &[Expr],
        limit: Option<usize>,
    ) -> Result<Vec<RecordBatch>>;

    fn get_pushdown_info(&self) -> PushdownInfo;

    async fn register_to_context(&self, ctx: &SessionContext, table_alias: &str) -> Result<()>;

    fn as_table_provider(
        &self,
        table: &str,
    ) -> Result<Arc<dyn TableProvider>>;
}

pub async fn create_datasource(
    config: DatasourceConfig,
) -> Result<Arc<dyn Datasource>> {
    let ds = match config.r#type() {
        DatasourceType::DatasourcePostgres => Arc::new(PostgresDatasource::new(config).await?) as Arc<dyn Datasource>,
        DatasourceType::DatasourceMysql => Arc::new(MysqlDatasource::new(config).await?) as Arc<dyn Datasource>,
        DatasourceType::DatasourceS3Parquet => Arc::new(S3ParquetDatasource::new(config).await?) as Arc<dyn Datasource>,
        DatasourceType::DatasourceLocalParquet => Arc::new(LocalParquetDatasource::new(config).await?) as Arc<dyn Datasource>,
        _ => return Err(DatasourceError::InvalidConfig(format!("unsupported datasource type: {:?}", config.r#type()))),
    };
    Ok(ds)
}

// -------------------------------------------------------------------------
// PostgreSQL Datasource
// -------------------------------------------------------------------------

pub struct PostgresDatasource {
    id: String,
    name: String,
    config: DatasourceConfig,
    conn: tokio::sync::Mutex<Option<tokio_postgres::Client>>,
}

impl PostgresDatasource {
    pub async fn new(config: DatasourceConfig) -> Result<Self> {
        let id = Uuid::new_v4().to_string();
        let name = config.name.clone();
        let conn = Self::connect(&config.connection_string).await?;
        Ok(Self {
            id,
            name,
            config,
            conn: tokio::sync::Mutex::new(Some(conn)),
        })
    }

    async fn connect(conn_str: &str) -> Result<tokio_postgres::Client> {
        let (client, connection) = tokio_postgres::connect(conn_str, tokio_postgres::NoTls)
            .await
            .map_err(|e| DatasourceError::Connection(format!("postgres connect: {e}")))?;

        tokio::spawn(async move {
            if let Err(e) = connection.await {
                error!(error = %e, "postgres connection error");
            }
        });

        Ok(client)
    }

    fn pg_type_to_arrow(ty: &tokio_postgres::types::Type) -> DataType {
        use tokio_postgres::types::Type;
        match ty {
            &Type::INT2 | &Type::INT4 | &Type::INT8 => DataType::Int64,
            &Type::FLOAT4 | &Type::FLOAT8 => DataType::Float64,
            &Type::BOOL => DataType::Boolean,
            &Type::VARCHAR | &Type::TEXT | &Type::BPCHAR | &Type::NAME => DataType::Utf8,
            &Type::TIMESTAMP | &Type::TIMESTAMPTZ => DataType::Timestamp(arrow::datatypes::TimeUnit::Millisecond, None),
            &Type::DATE => DataType::Date32,
            _ => DataType::Utf8,
        }
    }

    fn expr_to_sql(expr: &Expr) -> Option<String> {
        match expr {
            Expr::Column(c) => Some(c.name.clone()),
            Expr::Literal(v) => {
                match v.data_type() {
                    DataType::Utf8 => Some(format!("'{}'", v.to_string().replace('\'', "''"))),
                    _ => Some(v.to_string()),
                }
            }
            Expr::BinaryExpr(b) => {
                let left = Self::expr_to_sql(&b.left)?;
                let right = Self::expr_to_sql(&b.right)?;
                let op = match b.op {
                    datafusion::logical_expr::Operator::Eq => "=",
                    datafusion::logical_expr::Operator::NotEq => "!=",
                    datafusion::logical_expr::Operator::Lt => "<",
                    datafusion::logical_expr::Operator::LtEq => "<=",
                    datafusion::logical_expr::Operator::Gt => ">",
                    datafusion::logical_expr::Operator::GtEq => ">=",
                    datafusion::logical_expr::Operator::And => "AND",
                    datafusion::logical_expr::Operator::Or => "OR",
                    _ => return None,
                };
                Some(format!("({left} {op} {right})"))
            }
            Expr::InList(i) => {
                let col = Self::expr_to_sql(&i.expr)?;
                let values: Vec<_> = i.list.iter().filter_map(Self::expr_to_sql).collect();
                if values.len() != i.list.len() { return None; }
                let neg = if i.negated { "NOT " } else { "" };
                Some(format!("{col} {neg}IN ({})", values.join(", ")))
            }
            Expr::IsNull(c) => {
                let col = Self::expr_to_sql(c)?;
                Some(format!("{col} IS NULL"))
            }
            Expr::IsNotNull(c) => {
                let col = Self::expr_to_sql(c)?;
                Some(format!("{col} IS NOT NULL"))
            }
            _ => None,
        }
    }

    async fn row_to_batch(
        &self,
        rows: Vec<tokio_postgres::Row>,
        schema: SchemaRef,
    ) -> Result<RecordBatch> {
        if rows.is_empty() {
            return Ok(RecordBatch::new_empty(schema));
        }

        let mut arrays: Vec<ArrayRef> = Vec::with_capacity(schema.fields().len());
        for (i, field) in schema.fields().iter().enumerate() {
            let values: Vec<_> = rows.iter().map(|r| r.get::<usize, Option<String>>(i)).collect();
            let arr = match field.data_type() {
                DataType::Int64 => {
                    let v: Vec<Option<i64>> = values.iter().map(|s| s.as_ref().and_then(|x| x.parse::<i64>().ok())).collect();
                    Arc::new(Int64Array::from(v)) as ArrayRef
                }
                DataType::Float64 => {
                    let v: Vec<Option<f64>> = values.iter().map(|s| s.as_ref().and_then(|x| x.parse::<f64>().ok())).collect();
                    Arc::new(arrow_array::Float64Array::from(v)) as ArrayRef
                }
                DataType::Boolean => {
                    let v: Vec<Option<bool>> = values.iter().map(|s| s.as_ref().and_then(|x| x.parse::<bool>().ok())).collect();
                    Arc::new(arrow_array::BooleanArray::from(v)) as ArrayRef
                }
                _ => {
                    let v: Vec<Option<&str>> = values.iter().map(|s| s.as_deref()).collect();
                    Arc::new(StringArray::from(v)) as ArrayRef
                }
            };
            arrays.push(arr);
        }

        RecordBatch::try_new(schema, arrays)
            .map_err(|e| DatasourceError::Query(format!("build record batch: {e}")))
    }
}

#[async_trait]
impl Datasource for PostgresDatasource {
    fn id(&self) -> &str { &self.id }
    fn name(&self) -> &str { &self.name }
    fn datasource_type(&self) -> DatasourceType { DatasourceType::DatasourcePostgres }
    fn config(&self) -> &DatasourceConfig { &self.config }

    async fn list_tables(&self) -> Result<Vec<String>> {
        let conn = self.conn.lock().await;
        let client = conn.as_ref().ok_or_else(|| DatasourceError::Connection("not connected".into()))?;
        let rows = client.query(
            "SELECT tablename FROM pg_tables WHERE schemaname NOT IN ('pg_catalog', 'information_schema')",
            &[],
        ).await.map_err(|e| DatasourceError::Query(format!("list tables: {e}")))?;

        Ok(rows.iter().map(|r| r.get::<usize, String>(0)).collect())
    }

    async fn get_schema(&self, table: &str) -> Result<SchemaRef> {
        let conn = self.conn.lock().await;
        let client = conn.as_ref().ok_or_else(|| DatasourceError::Connection("not connected".into()))?;
        let rows = client.query(
            "SELECT column_name, data_type, is_nullable FROM information_schema.columns WHERE table_name = $1 ORDER BY ordinal_position",
            &[&table],
        ).await.map_err(|e| DatasourceError::Query(format!("get schema: {e}")))?;

        let mut fields = Vec::new();
        for row in rows {
            let col_name: String = row.get(0);
            let _data_type: String = row.get(1);
            let is_nullable: String = row.get(2);
            let nullable = is_nullable == "YES";
            fields.push(Field::new(col_name, DataType::Utf8, nullable));
        }

        Ok(Arc::new(Schema::new(fields)))
    }

    async fn execute_query(
        &self,
        table: &str,
        columns: &[String],
        predicates: &[Expr],
        limit: Option<usize>,
    ) -> Result<Vec<RecordBatch>> {
        let cols = if columns.is_empty() {
            "*".to_string()
        } else {
            columns.join(", ")
        };

        let mut sql = format!("SELECT {cols} FROM {table}");

        let pred_sql: Vec<_> = predicates.iter().filter_map(Self::expr_to_sql).collect();
        if !pred_sql.is_empty() {
            sql.push_str(" WHERE ");
            sql.push_str(&pred_sql.join(" AND "));
        }

        if let Some(lim) = limit {
            sql.push_str(&format!(" LIMIT {lim}"));
        }

        debug!(%sql, "executing postgres query");

        let conn = self.conn.lock().await;
        let client = conn.as_ref().ok_or_else(|| DatasourceError::Connection("not connected".into()))?;
        let rows = client.query(&sql, &[])
            .await.map_err(|e| DatasourceError::Query(format!("execute: {e}")))?;

        let schema = self.get_schema(table).await?;
        let batch = self.row_to_batch(rows, schema).await?;
        Ok(vec![batch])
    }

    fn get_pushdown_info(&self) -> PushdownInfo {
        PushdownInfo {
            can_push_predicates: true,
            can_push_projection: true,
            can_push_aggregation: true,
            can_push_join: false,
            pushed_predicates: Vec::new(),
            pushed_columns: Vec::new(),
        }
    }

    async fn register_to_context(&self, _ctx: &SessionContext, _table_alias: &str) -> Result<()> {
        Ok(())
    }

    fn as_table_provider(&self, _table: &str) -> Result<Arc<dyn TableProvider>> {
        Err(DatasourceError::Unsupported("Postgres table provider not implemented; use execute_query".into()))
    }
}

// -------------------------------------------------------------------------
// MySQL Datasource
// -------------------------------------------------------------------------

pub struct MysqlDatasource {
    id: String,
    name: String,
    config: DatasourceConfig,
    pool: tokio::sync::Mutex<Option<mysql_async::Pool>>,
}

impl MysqlDatasource {
    pub async fn new(config: DatasourceConfig) -> Result<Self> {
        let id = Uuid::new_v4().to_string();
        let name = config.name.clone();
        let opts = mysql_async::Opts::from_url(&config.connection_string)
            .map_err(|e| DatasourceError::InvalidConfig(format!("mysql url: {e}")))?;
        let pool = mysql_async::Pool::new(opts);
        Ok(Self {
            id,
            name,
            config,
            pool: tokio::sync::Mutex::new(Some(pool)),
        })
    }

    fn mysql_type_to_arrow(_col: &str) -> DataType {
        DataType::Utf8
    }
}

#[async_trait]
impl Datasource for MysqlDatasource {
    fn id(&self) -> &str { &self.id }
    fn name(&self) -> &str { &self.name }
    fn datasource_type(&self) -> DatasourceType { DatasourceType::DatasourceMysql }
    fn config(&self) -> &DatasourceConfig { &self.config }

    async fn list_tables(&self) -> Result<Vec<String>> {
        let pool = self.pool.lock().await;
        let pool = pool.as_ref().ok_or_else(|| DatasourceError::Connection("not connected".into()))?;
        let mut conn = pool.get_conn().await.map_err(|e| DatasourceError::Connection(format!("mysql connect: {e}")))?;
        let rows: Vec<String> = "SHOW TABLES".with(())
            .map(&mut conn, |row: mysql_async::Row| {
                row.get::<String, usize>(0).unwrap_or_default()
            }).await.map_err(|e| DatasourceError::Query(format!("mysql list tables: {e}")))?;
        Ok(rows)
    }

    async fn get_schema(&self, table: &str) -> Result<SchemaRef> {
        let pool = self.pool.lock().await;
        let pool = pool.as_ref().ok_or_else(|| DatasourceError::Connection("not connected".into()))?;
        let mut conn = pool.get_conn().await.map_err(|e| DatasourceError::Connection(format!("mysql connect: {e}")))?;
        let rows: Vec<(String, String, String)> = format!("DESCRIBE {}", table).with(())
            .map(&mut conn, |row: mysql_async::Row| {
                (
                    row.get::<String, usize>(0).unwrap_or_default(),
                    row.get::<String, usize>(1).unwrap_or_default(),
                    row.get::<String, usize>(2).unwrap_or_default(),
                )
            }).await.map_err(|e| DatasourceError::Query(format!("mysql describe: {e}")))?;

        let fields: Vec<Field> = rows.iter()
            .map(|(name, _ty, nullable)| {
                Field::new(name, DataType::Utf8, nullable == "YES")
            })
            .collect();

        Ok(Arc::new(Schema::new(fields)))
    }

    async fn execute_query(
        &self,
        table: &str,
        columns: &[String],
        predicates: &[Expr],
        limit: Option<usize>,
    ) -> Result<Vec<RecordBatch>> {
        let cols = if columns.is_empty() { "*".to_string() } else { columns.join(", ") };
        let mut sql = format!("SELECT {cols} FROM {table}");
        let pred_sql: Vec<_> = predicates.iter().filter_map(PostgresDatasource::expr_to_sql).collect();
        if !pred_sql.is_empty() {
            sql.push_str(" WHERE ");
            sql.push_str(&pred_sql.join(" AND "));
        }
        if let Some(lim) = limit {
            sql.push_str(&format!(" LIMIT {lim}"));
        }

        debug!(%sql, "executing mysql query");

        let pool = self.pool.lock().await;
        let pool = pool.as_ref().ok_or_else(|| DatasourceError::Connection("not connected".into()))?;
        let mut conn = pool.get_conn().await.map_err(|e| DatasourceError::Connection(format!("mysql connect: {e}")))?;
        let schema = self.get_schema(table).await?;
        let rows: Vec<Vec<Option<String>>> = sql.with(())
            .map(&mut conn, |row: mysql_async::Row| {
                let mut v = Vec::new();
                for i in 0..schema.fields().len() {
                    v.push(row.get::<Option<String>, usize>(i));
                }
                v
            }).await.map_err(|e| DatasourceError::Query(format!("mysql query: {e}")))?;

        if rows.is_empty() {
            return Ok(vec![RecordBatch::new_empty(schema)]);
        }

        let mut arrays: Vec<ArrayRef> = Vec::with_capacity(schema.fields().len());
        for (i, field) in schema.fields().iter().enumerate() {
            match field.data_type() {
                DataType::Int64 => {
                    let v: Vec<Option<i64>> = rows.iter().map(|r| r[i].as_ref().and_then(|x| x.parse::<i64>().ok())).collect();
                    arrays.push(Arc::new(Int64Array::from(v)) as ArrayRef);
                }
                DataType::Float64 => {
                    let v: Vec<Option<f64>> = rows.iter().map(|r| r[i].as_ref().and_then(|x| x.parse::<f64>().ok())).collect();
                    arrays.push(Arc::new(arrow_array::Float64Array::from(v)) as ArrayRef);
                }
                _ => {
                    let v: Vec<Option<&str>> = rows.iter().map(|r| r[i].as_deref()).collect();
                    arrays.push(Arc::new(StringArray::from(v)) as ArrayRef);
                }
            }
        }

        let batch = RecordBatch::try_new(schema, arrays)
            .map_err(|e| DatasourceError::Query(format!("build batch: {e}")))?;
        Ok(vec![batch])
    }

    fn get_pushdown_info(&self) -> PushdownInfo {
        PushdownInfo {
            can_push_predicates: true,
            can_push_projection: true,
            can_push_aggregation: true,
            can_push_join: false,
            pushed_predicates: Vec::new(),
            pushed_columns: Vec::new(),
        }
    }

    async fn register_to_context(&self, _ctx: &SessionContext, _table_alias: &str) -> Result<()> {
        Ok(())
    }

    fn as_table_provider(&self, _table: &str) -> Result<Arc<dyn TableProvider>> {
        Err(DatasourceError::Unsupported("MySQL table provider not implemented".into()))
    }
}

// -------------------------------------------------------------------------
// S3 Parquet Datasource (using DataFusion's built-in S3 support)
// -------------------------------------------------------------------------

pub struct S3ParquetDatasource {
    id: String,
    name: String,
    config: DatasourceConfig,
    bucket: String,
    prefix: String,
    region: String,
}

impl S3ParquetDatasource {
    pub async fn new(config: DatasourceConfig) -> Result<Self> {
        let id = Uuid::new_v4().to_string();
        let name = config.name.clone();

        let bucket = config.options.get("bucket")
            .ok_or_else(|| DatasourceError::InvalidConfig("missing bucket option".into()))?.clone();
        let prefix = config.options.get("prefix").cloned().unwrap_or_default();
        let region = config.options.get("region").cloned().unwrap_or_else(|| "us-east-1".into());

        Ok(Self { id, name, config, bucket, prefix, region })
    }

    fn s3_url(&self, key: &str) -> String {
        format!("s3://{}/{}{}", self.bucket, self.prefix, key)
    }
}

#[async_trait]
impl Datasource for S3ParquetDatasource {
    fn id(&self) -> &str { &self.id }
    fn name(&self) -> &str { &self.name }
    fn datasource_type(&self) -> DatasourceType { DatasourceType::DatasourceS3Parquet }
    fn config(&self) -> &DatasourceConfig { &self.config }

    async fn list_tables(&self) -> Result<Vec<String>> {
        Ok(vec!["parquet".into()])
    }

    async fn get_schema(&self, table: &str) -> Result<SchemaRef> {
        let ctx = SessionContext::new();
        let url = self.s3_url(table);
        let df = ctx.read_parquet(url, ParquetReadOptions::default())
            .await.map_err(|e| DatasourceError::Schema(format!("read parquet schema: {e}")))?;
        Ok(df.schema().into())
    }

    async fn execute_query(
        &self,
        table: &str,
        columns: &[String],
        predicates: &[Expr],
        limit: Option<usize>,
    ) -> Result<Vec<RecordBatch>> {
        let ctx = SessionContext::new();
        let url = self.s3_url(table);
        let mut df = ctx.read_parquet(url, ParquetReadOptions::default())
            .await.map_err(|e| DatasourceError::Query(format!("read parquet: {e}")))?;

        if !columns.is_empty() {
            let cols: Vec<_> = columns.iter().map(|c| col(c)).collect();
            df = df.select(cols).map_err(|e| DatasourceError::Query(format!("select: {e}")))?;
        }

        for pred in predicates {
            df = df.filter(pred.clone()).map_err(|e| DatasourceError::Query(format!("filter: {e}")))?;
        }

        if let Some(lim) = limit {
            df = df.limit(0, Some(lim)).map_err(|e| DatasourceError::Query(format!("limit: {e}")))?;
        }

        let results = df.collect().await.map_err(|e| DatasourceError::Query(format!("collect: {e}")))?;
        Ok(results)
    }

    fn get_pushdown_info(&self) -> PushdownInfo {
        PushdownInfo {
            can_push_predicates: true,
            can_push_projection: true,
            can_push_aggregation: true,
            can_push_join: false,
            pushed_predicates: Vec::new(),
            pushed_columns: Vec::new(),
        }
    }

    async fn register_to_context(&self, ctx: &SessionContext, table_alias: &str) -> Result<()> {
        let url = self.s3_url("");
        ctx.register_parquet(table_alias, &url, ParquetReadOptions::default())
            .await.map_err(|e| DatasourceError::Query(format!("register s3 parquet: {e}")))?;
        Ok(())
    }

    fn as_table_provider(&self, table: &str) -> Result<Arc<dyn TableProvider>> {
        let ctx = SessionContext::new();
        let url = self.s3_url(table);
        let df = futures::executor::block_on(async {
            ctx.read_parquet(url, ParquetReadOptions::default()).await
        }).map_err(|e| DatasourceError::Query(format!("read parquet: {e}")))?;
        Ok(df.into_view())
    }
}

// -------------------------------------------------------------------------
// Local Parquet Datasource
// -------------------------------------------------------------------------

pub struct LocalParquetDatasource {
    id: String,
    name: String,
    config: DatasourceConfig,
    base_path: String,
}

impl LocalParquetDatasource {
    pub async fn new(config: DatasourceConfig) -> Result<Self> {
        let id = Uuid::new_v4().to_string();
        let name = config.name.clone();
        let base_path = config.options.get("path")
            .ok_or_else(|| DatasourceError::InvalidConfig("missing path option".into()))?.clone();
        Ok(Self { id, name, config, base_path })
    }
}

#[async_trait]
impl Datasource for LocalParquetDatasource {
    fn id(&self) -> &str { &self.id }
    fn name(&self) -> &str { &self.name }
    fn datasource_type(&self) -> DatasourceType { DatasourceType::DatasourceLocalParquet }
    fn config(&self) -> &DatasourceConfig { &self.config }

    async fn list_tables(&self) -> Result<Vec<String>> {
        let mut entries = tokio::fs::read_dir(&self.base_path)
            .await.map_err(|e| DatasourceError::Query(format!("read dir: {e}")))?;
        let mut tables = Vec::new();
        while let Some(entry) = entries.next_entry()
            .await.map_err(|e| DatasourceError::Query(format!("read entry: {e}")))?
        {
            if let Some(name) = entry.file_name().to_str() {
                if name.ends_with(".parquet") {
                    tables.push(name.trim_end_matches(".parquet").to_string());
                }
            }
        }
        Ok(tables)
    }

    async fn get_schema(&self, table: &str) -> Result<SchemaRef> {
        let path = format!("{}/{}.parquet", self.base_path, table);
        let ctx = SessionContext::new();
        let df = ctx.read_parquet(path, ParquetReadOptions::default())
            .await.map_err(|e| DatasourceError::Schema(format!("read parquet schema: {e}")))?;
        Ok(df.schema().into())
    }

    async fn execute_query(
        &self,
        table: &str,
        columns: &[String],
        predicates: &[Expr],
        limit: Option<usize>,
    ) -> Result<Vec<RecordBatch>> {
        let path = format!("{}/{}.parquet", self.base_path, table);
        let ctx = SessionContext::new();
        let mut df = ctx.read_parquet(path, ParquetReadOptions::default())
            .await.map_err(|e| DatasourceError::Query(format!("read parquet: {e}")))?;

        if !columns.is_empty() {
            let cols: Vec<_> = columns.iter().map(|c| col(c)).collect();
            df = df.select(cols).map_err(|e| DatasourceError::Query(format!("select: {e}")))?;
        }

        for pred in predicates {
            df = df.filter(pred.clone()).map_err(|e| DatasourceError::Query(format!("filter: {e}")))?;
        }

        if let Some(lim) = limit {
            df = df.limit(0, Some(lim)).map_err(|e| DatasourceError::Query(format!("limit: {e}")))?;
        }

        let results = df.collect().await.map_err(|e| DatasourceError::Query(format!("collect: {e}")))?;
        Ok(results)
    }

    fn get_pushdown_info(&self) -> PushdownInfo {
        PushdownInfo {
            can_push_predicates: true,
            can_push_projection: true,
            can_push_aggregation: true,
            can_push_join: false,
            pushed_predicates: Vec::new(),
            pushed_columns: Vec::new(),
        }
    }

    async fn register_to_context(&self, ctx: &SessionContext, table_alias: &str) -> Result<()> {
        ctx.register_parquet(table_alias, &self.base_path, ParquetReadOptions::default())
            .await.map_err(|e| DatasourceError::Query(format!("register parquet: {e}")))?;
        Ok(())
    }

    fn as_table_provider(&self, table: &str) -> Result<Arc<dyn TableProvider>> {
        let path = format!("{}/{}.parquet", self.base_path, table);
        let ctx = SessionContext::new();
        let df = futures::executor::block_on(async {
            ctx.read_parquet(path, ParquetReadOptions::default()).await
        }).map_err(|e| DatasourceError::Query(format!("read parquet: {e}")))?;
        Ok(df.into_view())
    }
}

// -------------------------------------------------------------------------
// Datasource Registry
// -------------------------------------------------------------------------

#[derive(Clone)]
pub struct DatasourceRegistry {
    datasources: Arc<dashmap::DashMap<String, Arc<dyn Datasource>>>,
}

impl DatasourceRegistry {
    pub fn new() -> Self {
        Self { datasources: Arc::new(dashmap::DashMap::new()) }
    }

    pub fn register(&self, datasource: Arc<dyn Datasource>) {
        self.datasources.insert(datasource.name().to_string(), datasource);
    }

    pub fn get(&self, name: &str) -> Option<Arc<dyn Datasource>> {
        self.datasources.get(name).map(|d| d.value().clone())
    }

    pub fn drop(&self, name: &str) -> bool {
        self.datasources.remove(name).is_some()
    }

    pub fn list(&self) -> Vec<DatasourceConfig> {
        self.datasources.iter().map(|d| d.config().clone()).collect()
    }

    pub fn len(&self) -> usize {
        self.datasources.len()
    }

    pub fn is_empty(&self) -> bool {
        self.datasources.is_empty()
    }
}

impl Default for DatasourceRegistry {
    fn default() -> Self {
        Self::new()
    }
}
