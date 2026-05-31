use std::time::{Duration, Instant};

use rusqlite::types::Value;
use rusqlite::Connection;
use serde::Serialize;
use tracing::warn;

use crate::pool::{CheckpointResult, PoolResult, SqlitePool};

#[derive(Debug, Clone, Serialize)]
pub struct QueryResponse {
    pub columns: Vec<String>,
    pub rows: Vec<Vec<serde_json::Value>>,
    pub rows_affected: u64,
    pub last_insert_rowid: Option<i64>,
    pub truncated: bool,
}

pub struct QueryExecutor {
    pool: SqlitePool,
    max_return_rows: usize,
    slow_query_threshold: Duration,
}

impl QueryExecutor {
    pub fn new(pool: SqlitePool, max_return_rows: usize, slow_query_threshold: Duration) -> Self {
        QueryExecutor {
            pool,
            max_return_rows,
            slow_query_threshold,
        }
    }

    pub fn pool(&self) -> &SqlitePool {
        &self.pool
    }

    pub async fn execute(&self, sql: String) -> PoolResult<QueryResponse> {
        let start = Instant::now();
        let trimmed = sql.trim().to_ascii_uppercase();
        let is_read = trimmed.starts_with("SELECT") || trimmed.starts_with("PRAGMA")
            || trimmed.starts_with("EXPLAIN");

        let max_return_rows = self.max_return_rows;
        let result = if is_read {
            self.pool
                .read(move |conn| run_read_query(conn, &sql, max_return_rows))
                .await?
        } else {
            self.pool
                .write(move |conn| run_write_query(conn, &sql, max_return_rows))
                .await?
        };

        let elapsed = start.elapsed();
        if elapsed >= self.slow_query_threshold {
            warn!(
                elapsed_ms = elapsed.as_millis() as u64,
                sql = %sql,
                "slow query detected"
            );
        }

        Ok(result)
    }

    pub async fn checkpoint(&self, truncate: bool) -> PoolResult<CheckpointResult> {
        self.pool.trigger_checkpoint(truncate).await
    }
}

fn run_read_query(
    conn: &Connection,
    sql: &str,
    max_return_rows: usize,
) -> Result<QueryResponse, rusqlite::Error> {
    let mut stmt = conn.prepare(sql)?;
    let columns: Vec<String> = stmt.columns().iter().map(|c| c.name().to_string()).collect();

    let mut rows = stmt.query([])?;
    let mut out = Vec::new();
    let mut truncated = false;

    while let Some(row) = rows.next()? {
        if out.len() >= max_return_rows {
            truncated = true;
            break;
        }
        let mut r = Vec::with_capacity(columns.len());
        for i in 0..columns.len() {
            let v: Value = row.get(i)?;
            r.push(value_to_json(v));
        }
        out.push(r);
    }

    Ok(QueryResponse {
        columns,
        rows: out,
        rows_affected: 0,
        last_insert_rowid: None,
        truncated,
    })
}

fn run_write_query(
    conn: &Connection,
    sql: &str,
    max_return_rows: usize,
) -> Result<QueryResponse, rusqlite::Error> {
    let mut stmt = conn.prepare(sql)?;
    let columns: Vec<String> = stmt.columns().iter().map(|c| c.name().to_string()).collect();

    let has_result_set = !columns.is_empty();

    if has_result_set {
        let mut rows = stmt.query([])?;
        let mut out = Vec::new();
        let mut truncated = false;
        while let Some(row) = rows.next()? {
            if out.len() >= max_return_rows {
                truncated = true;
                break;
            }
            let mut r = Vec::with_capacity(columns.len());
            for i in 0..columns.len() {
                let v: Value = row.get(i)?;
                r.push(value_to_json(v));
            }
            out.push(r);
        }
        Ok(QueryResponse {
            columns,
            rows: out,
            rows_affected: conn.changes(),
            last_insert_rowid: Some(conn.last_insert_rowid()),
            truncated,
        })
    } else {
        stmt.execute([])?;
        Ok(QueryResponse {
            columns: vec![],
            rows: vec![],
            rows_affected: conn.changes(),
            last_insert_rowid: Some(conn.last_insert_rowid()),
            truncated: false,
        })
    }
}

fn value_to_json(v: Value) -> serde_json::Value {
    match v {
        Value::Null => serde_json::Value::Null,
        Value::Integer(i) => serde_json::Value::Number(i.into()),
        Value::Real(x) => serde_json::Number::from_f64(x)
            .map(serde_json::Value::Number)
            .unwrap_or(serde_json::Value::Null),
        Value::Text(s) => serde_json::Value::String(s),
        Value::Blob(b) => serde_json::Value::String(encode_hex(&b)),
    }
}

fn encode_hex(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut out = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        out.push(HEX[(b >> 4) as usize] as char);
        out.push(HEX[(b & 0x0f) as usize] as char);
    }
    out
}
