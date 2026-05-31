use std::sync::Arc;

use axum::extract::State;
use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use serde::{Deserialize, Serialize};

use crate::error::ErrorBody;
use crate::pool::{current_generation, Role};
use crate::query::{QueryExecutor, QueryResponse};
use crate::replication::{build_snapshot_body, ReplicationState, ReplicationStatus};

pub struct AppState {
    pub executor: Arc<QueryExecutor>,
    pub replication: Arc<ReplicationState>,
}

pub fn router(state: AppState) -> Router {
    Router::new()
        .route("/query", post(handle_query))
        .route("/checkpoint", post(handle_checkpoint))
        .route("/health", get(handle_health))
        .route("/repl/status", get(handle_repl_status))
        .route("/repl/snapshot", get(handle_repl_snapshot))
        .route("/repl/failover", post(handle_failover))
        .with_state(state)
}

#[derive(Debug, Deserialize)]
pub struct QueryRequest {
    pub sql: String,
}

#[derive(Debug, Serialize)]
pub struct QueryBody {
    pub ok: bool,
    #[serde(flatten)]
    pub data: Option<QueryResponse>,
    pub error: Option<String>,
}

async fn handle_query(
    State(state): State<AppState>,
    Json(req): Json<QueryRequest>,
) -> Response {
    if req.sql.trim().is_empty() {
        return error_response(StatusCode::BAD_REQUEST, "sql is empty");
    }

    match state.executor.execute(req.sql).await {
        Ok(resp) => Json(QueryBody {
            ok: true,
            data: Some(resp),
            error: None,
        })
        .into_response(),
        Err(e) => error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()),
    }
}

#[derive(Debug, Deserialize)]
pub struct CheckpointRequest {
    #[serde(default = "default_truncate")]
    pub truncate: bool,
}

fn default_truncate() -> bool {
    true
}

#[derive(Debug, Serialize)]
pub struct CheckpointBody {
    pub ok: bool,
    pub truncated: bool,
    pub before_bytes: u64,
    pub after_bytes: u64,
    pub checkpointed_pages: i64,
    pub synced_pages: i64,
}

async fn handle_checkpoint(
    State(state): State<AppState>,
    Json(req): Json<CheckpointRequest>,
) -> Response {
    match state.executor.checkpoint(req.truncate).await {
        Ok(r) => Json(CheckpointBody {
            ok: true,
            truncated: r.truncated,
            before_bytes: r.before_bytes,
            after_bytes: r.after_bytes,
            checkpointed_pages: r.checkpointed_pages,
            synced_pages: r.synced_pages,
        })
        .into_response(),
        Err(e) => error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()),
    }
}

async fn handle_health(State(state): State<AppState>) -> Response {
    let wal_bytes = state.executor.pool().wal_size_on_disk();
    Json(serde_json::json!({
        "status": "ok",
        "wal_bytes": wal_bytes,
        "role": state.executor.pool().role().as_str(),
        "generation": current_generation(),
    }))
    .into_response()
}

async fn handle_repl_status(State(state): State<AppState>) -> Response {
    let s: ReplicationStatus = state.replication.status();
    Json(s).into_response()
}

async fn handle_repl_snapshot(State(state): State<AppState>) -> Response {
    if state.executor.pool().role() != Role::Master {
        return error_response(
            StatusCode::BAD_REQUEST,
            "snapshot only available on master node",
        );
    }
    let pool = state.executor.pool().clone();
    match tokio::task::spawn_blocking(move || pool.export_snapshot())
        .await
        .map_err(|e| crate::error::AppError::Pool(e.to_string()))
    {
        Ok(Ok(bytes)) => {
            let gen = current_generation();
            let body = build_snapshot_body(gen, &bytes);
            (
                StatusCode::OK,
                [("content-type", "application/octet-stream")],
                body,
            )
                .into_response()
        }
        Ok(Err(e)) => error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()),
        Err(e) => error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()),
    }
}

#[derive(Debug, Deserialize)]
pub struct FailoverRequest {
    pub action: FailoverAction,
    #[serde(default)]
    pub upstream: Option<String>,
}

#[derive(Debug, Deserialize, Clone, Copy)]
#[serde(rename_all = "lowercase")]
pub enum FailoverAction {
    Promote,
    Demote,
}

#[derive(Debug, Serialize)]
pub struct FailoverBody {
    pub ok: bool,
    pub role: String,
}

async fn handle_failover(
    State(state): State<AppState>,
    Json(req): Json<FailoverRequest>,
) -> Response {
    match req.action {
        FailoverAction::Promote => {
            match state.replication.pool.promote_to_master().await {
                Ok(()) => {
                    *state.replication.pool.master_url.write().unwrap() = None;
                    Json(FailoverBody {
                        ok: true,
                        role: "master".into(),
                    })
                    .into_response()
                }
                Err(e) => error_response(
                    StatusCode::INTERNAL_SERVER_ERROR,
                    &e.to_string(),
                ),
            }
        }
        FailoverAction::Demote => {
            let upstream = match req.upstream {
                Some(u) => u,
                None => {
                    return error_response(
                        StatusCode::BAD_REQUEST,
                        "demote requires upstream url",
                    )
                }
            };
            match state.replication.pool.demote_to_slave().await {
                Ok(()) => {
                    *state.replication.pool.master_url.write().unwrap() =
                        Some(upstream);
                    Json(FailoverBody {
                        ok: true,
                        role: "slave".into(),
                    })
                    .into_response()
                }
                Err(e) => error_response(
                    StatusCode::INTERNAL_SERVER_ERROR,
                    &e.to_string(),
                ),
            }
        }
    }
}

fn error_response(code: StatusCode, msg: &str) -> Response {
    (
        code,
        Json(ErrorBody {
            error: msg.to_string(),
            code: code.as_u16(),
        }),
    )
        .into_response()
}
