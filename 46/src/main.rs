mod error;
mod http;
mod pool;
mod query;
mod replication;

use std::env;
use std::sync::Arc;

use tracing::info;
use tracing_subscriber::EnvFilter;

use crate::error::AppError;
use crate::http::AppState;
use crate::pool::{Role, SqlitePool};
use crate::query::QueryExecutor;
use crate::replication::ReplicationState;

type AppResult<T> = Result<T, AppError>;

fn main() -> AppResult<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info")),
        )
        .with_target(false)
        .init();

    let db_path = env::var("SQLITE_DB_PATH").unwrap_or_else(|_| "pool.db".to_string());
    let addr = env::var("LISTEN_ADDR").unwrap_or_else(|_| "127.0.0.1:3000".to_string());
    let max_return_rows: usize = env::var("MAX_RETURN_ROWS")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(1000);
    let slow_query_ms: u64 = env::var("SLOW_QUERY_MS")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(500);
    let role_str = env::var("ROLE").unwrap_or_else(|_| "master".to_string());
    let role = match role_str.as_str() {
        "slave" => Role::Slave,
        _ => Role::Master,
    };
    let master_url = env::var("MASTER_URL").ok();

    info!(
        db_path = %db_path,
        addr = %addr,
        max_return_rows,
        slow_query_ms,
        role = role.as_str(),
        master_url = ?master_url,
        "starting sqlite-pool service"
    );

    let rt = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .map_err(|e| AppError::Startup(e.to_string()))?;

    rt.block_on(async move {
        let mut builder = SqlitePool::builder()
            .path(&db_path)
            .read_pool_size(4)
            .max_busy_retries(5)
            .role(role);
        if let Some(u) = &master_url {
            builder = builder.master_url(u.clone());
        }
        let pool = builder.build().await?;
        let pool = Arc::new(pool);

        let executor = Arc::new(QueryExecutor::new(
            pool.clone(),
            max_return_rows,
            std::time::Duration::from_millis(slow_query_ms),
        ));

        let rep = Arc::new(ReplicationState::new(pool.clone()));

        if role == Role::Slave && master_url.is_some() {
            let rep_clone = rep.clone();
            tokio::spawn(async move {
                replication::run_pull_task(rep_clone).await;
            });
        }

        let app_state = AppState {
            executor,
            replication: rep,
        };
        let app = http::router(app_state);

        let listener = tokio::net::TcpListener::bind(&addr)
            .await
            .map_err(|e| AppError::Startup(e.to_string()))?;
        info!(addr = %addr, "listening");
        axum::serve(listener, app)
            .await
            .map_err(|e| AppError::Startup(e.to_string()))?;

        Ok::<(), AppError>(())
    })
}
