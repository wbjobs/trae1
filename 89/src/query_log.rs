use std::collections::VecDeque;
use std::sync::Arc;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use tracing::*;
use uuid::Uuid;

use crate::federated_optimizer::OptimizerConfig;
use crate::proto::QueryStats;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct QueryLogEntry {
    pub query_id: String,
    pub sql: String,
    pub start_time_ms: u64,
    pub end_time_ms: u64,
    pub elapsed_ms: u64,
    pub rows_returned: u64,
    pub is_slow: bool,
    pub accessed_datasources: Vec<String>,
    pub success: bool,
    pub error: Option<String>,
    pub execution_plan_json: Option<String>,
}

impl QueryLogEntry {
    pub fn to_proto(&self) -> QueryStats {
        QueryStats {
            query_id: self.query_id.clone(),
            sql: self.sql.clone(),
            start_time_ms: self.start_time_ms as i64,
            elapsed_ms: self.elapsed_ms as i64,
            row_count: self.rows_returned as i64,
            is_slow: self.is_slow,
            accessed_datasources: self.accessed_datasources.clone(),
            error: self.error.clone().unwrap_or_default(),
        }
    }
}

pub struct QueryLogger {
    config: OptimizerConfig,
    entries: Mutex<VecDeque<QueryLogEntry>>,
    max_entries: usize,
}

impl QueryLogger {
    pub fn new(config: OptimizerConfig, max_entries: usize) -> Self {
        Self {
            config,
            entries: Mutex::new(VecDeque::with_capacity(max_entries)),
            max_entries,
        }
    }

    pub fn start_query(&self, sql: &str) -> QueryHandle {
        let query_id = Uuid::new_v4().to_string();
        let start_time = Instant::now();
        let start_time_ms = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or(Duration::ZERO)
            .as_millis() as u64;

        debug!(query_id, sql, "query started");

        QueryHandle {
            query_id,
            sql: sql.to_string(),
            start_time,
            start_time_ms,
            rows_returned: 0,
            accessed_datasources: Vec::new(),
            execution_plan_json: None,
            logger: unsafe { std::mem::transmute::<&QueryLogger, &'static QueryLogger>(self) },
        }
    }

    fn finish_query(&self, handle: QueryHandle, success: bool, error: Option<String>) {
        let elapsed = handle.start_time.elapsed();
        let elapsed_ms = elapsed.as_millis() as u64;
        let end_time_ms = handle.start_time_ms + elapsed_ms;
        let is_slow = elapsed_ms > self.config.slow_query_threshold_ms;

        let entry = QueryLogEntry {
            query_id: handle.query_id.clone(),
            sql: handle.sql,
            start_time_ms: handle.start_time_ms,
            end_time_ms,
            elapsed_ms,
            rows_returned: handle.rows_returned,
            is_slow,
            accessed_datasources: handle.accessed_datasources,
            success,
            error,
            execution_plan_json: handle.execution_plan_json,
        };

        if is_slow {
            warn!(
                query_id = entry.query_id,
                elapsed_ms,
                rows_returned = entry.rows_returned,
                "SLOW QUERY (>{})",
                self.config.slow_query_threshold_ms
            );
            debug!(query_id = entry.query_id, sql = entry.sql, "slow query SQL");
        } else {
            debug!(
                query_id = entry.query_id,
                elapsed_ms,
                rows_returned = entry.rows_returned,
                success,
                "query finished"
            );
        }

        let mut entries = self.entries.lock();
        entries.push_front(entry);
        while entries.len() > self.max_entries {
            entries.pop_back();
        }
    }

    pub fn get_slow_queries(&self, limit: usize) -> Vec<QueryLogEntry> {
        let entries = self.entries.lock();
        entries.iter()
            .filter(|e| e.is_slow)
            .take(limit)
            .cloned()
            .collect()
    }

    pub fn get_recent_queries(&self, limit: usize) -> Vec<QueryLogEntry> {
        let entries = self.entries.lock();
        entries.iter()
            .take(limit)
            .cloned()
            .collect()
    }
}

pub struct QueryHandle {
    pub query_id: String,
    pub sql: String,
    pub start_time: Instant,
    pub start_time_ms: u64,
    pub rows_returned: u64,
    pub accessed_datasources: Vec<String>,
    pub execution_plan_json: Option<String>,
    logger: &'static QueryLogger,
}

impl QueryHandle {
    pub fn add_datasource(&mut self, name: &str) {
        if !self.accessed_datasources.iter().any(|d| d == name) {
            self.accessed_datasources.push(name.to_string());
        }
    }

    pub fn set_execution_plan(&mut self, plan_json: String) {
        self.execution_plan_json = Some(plan_json);
    }

    pub fn add_rows(&mut self, count: u64) {
        self.rows_returned += count;
    }

    pub fn is_timed_out(&self, timeout_sec: u64) -> bool {
        self.start_time.elapsed().as_secs() > timeout_sec
    }

    pub fn finish(self, success: bool) {
        self.logger.finish_query(self, success, None);
    }

    pub fn finish_with_error(self, error: &str) {
        self.logger.finish_query(self, false, Some(error.to_string()));
    }
}

unsafe impl Send for QueryHandle {}
unsafe impl Sync for QueryHandle {}
