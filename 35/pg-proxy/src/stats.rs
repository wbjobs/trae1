use crate::compression::CompressionStats;
use parking_lot::RwLock;
use serde::Serialize;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant};

#[derive(Debug, Clone, Default, Serialize)]
pub struct ConnectionStats {
    pub total_connections: u64,
    pub active_connections: u64,
    pub total_queries: u64,
    pub insert_queries: u64,
    pub update_queries: u64,
    pub select_queries: u64,
    pub other_queries: u64,
    pub bytes_from_client: u64,
    pub bytes_to_client: u64,
    pub bytes_to_server: u64,
    pub bytes_from_server: u64,
    #[serde(skip)]
    pub start_time: Option<Instant>,
    #[serde(skip)]
    pub last_query_time: Option<Instant>,
}

impl ConnectionStats {
    pub fn new() -> Self {
        Self {
            start_time: Some(Instant::now()),
            ..Default::default()
        }
    }

    pub fn uptime(&self) -> Duration {
        self.start_time
            .map(|t| t.elapsed())
            .unwrap_or(Duration::from_secs(0))
    }

    pub fn format_uptime(&self) -> String {
        let uptime = self.uptime();
        let hours = uptime.as_secs() / 3600;
        let minutes = (uptime.as_secs() % 3600) / 60;
        let seconds = uptime.as_secs() % 60;
        format!("{}h {}m {}s", hours, minutes, seconds)
    }

    pub fn queries_per_second(&self) -> f64 {
        let uptime = self.uptime().as_secs_f64();
        if uptime > 0.0 {
            self.total_queries as f64 / uptime
        } else {
            0.0
        }
    }

    pub fn throughput_mbps(&self) -> (f64, f64) {
        let uptime = self.uptime().as_secs_f64();
        if uptime > 0.0 {
            let to_client_mbps = (self.bytes_to_client as f64 * 8.0) / uptime / 1_000_000.0;
            let from_client_mbps = (self.bytes_from_client as f64 * 8.0) / uptime / 1_000_000.0;
            (to_client_mbps, from_client_mbps)
        } else {
            (0.0, 0.0)
        }
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct InstanceStats {
    pub name: String,
    pub connections: ConnectionStats,
    pub compression: CompressionStats,
}

impl InstanceStats {
    pub fn new(name: &str) -> Self {
        Self {
            name: name.to_string(),
            connections: ConnectionStats::new(),
            compression: CompressionStats::new(),
        }
    }
}

#[derive(Debug, Default)]
pub struct StatsStore {
    instances: RwLock<HashMap<String, InstanceStats>>,
}

impl StatsStore {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn register_instance(&self, name: &str) {
        let mut instances = self.instances.write();
        instances
            .entry(name.to_string())
            .or_insert_with(|| InstanceStats::new(name));
    }

    pub fn get_stats(&self, name: &str) -> Option<InstanceStats> {
        self.instances.read().get(name).cloned()
    }

    pub fn get_all_stats(&self) -> Vec<InstanceStats> {
        self.instances.read().values().cloned().collect()
    }

    pub fn record_connection(&self, name: &str) {
        if let Some(stats) = self.instances.write().get_mut(name) {
            stats.connections.total_connections += 1;
            stats.connections.active_connections += 1;
        }
    }

    pub fn record_disconnection(&self, name: &str) {
        if let Some(stats) = self.instances.write().get_mut(name) {
            if stats.connections.active_connections > 0 {
                stats.connections.active_connections -= 1;
            }
        }
    }

    pub fn record_query(&self, name: &str, query_type: QueryType) {
        if let Some(stats) = self.instances.write().get_mut(name) {
            stats.connections.total_queries += 1;
            stats.connections.last_query_time = Some(Instant::now());
            match query_type {
                QueryType::Insert => stats.connections.insert_queries += 1,
                QueryType::Update => stats.connections.update_queries += 1,
                QueryType::Select => stats.connections.select_queries += 1,
                QueryType::Other => stats.connections.other_queries += 1,
            }
        }
    }

    pub fn record_bytes_from_client(&self, name: &str, bytes: u64) {
        if let Some(stats) = self.instances.write().get_mut(name) {
            stats.connections.bytes_from_client += bytes;
        }
    }

    pub fn record_bytes_to_client(&self, name: &str, bytes: u64) {
        if let Some(stats) = self.instances.write().get_mut(name) {
            stats.connections.bytes_to_client += bytes;
        }
    }

    pub fn record_bytes_to_server(&self, name: &str, bytes: u64) {
        if let Some(stats) = self.instances.write().get_mut(name) {
            stats.connections.bytes_to_server += bytes;
        }
    }

    pub fn record_bytes_from_server(&self, name: &str, bytes: u64) {
        if let Some(stats) = self.instances.write().get_mut(name) {
            stats.connections.bytes_from_server += bytes;
        }
    }

    pub fn record_compression(&self, name: &str, original: u64, compressed: u64) {
        if let Some(stats) = self.instances.write().get_mut(name) {
            stats.compression.record_compression(original, compressed);
        }
    }

    pub fn record_decompression(&self, name: &str, original: u64, decompressed: u64) {
        if let Some(stats) = self.instances.write().get_mut(name) {
            stats.compression.record_decompression(original, decompressed);
        }
    }

    pub fn record_skipped(&self, name: &str, size: u64, reason: crate::compression::CompressDecision) {
        if let Some(stats) = self.instances.write().get_mut(name) {
            stats.compression.record_skipped(size, reason);
        }
    }
}

pub type SharedStats = Arc<StatsStore>;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum QueryType {
    Insert,
    Update,
    Select,
    Other,
}

pub fn classify_query(query: &str) -> QueryType {
    let trimmed = query.trim_start().to_uppercase();
    if trimmed.starts_with("INSERT") {
        QueryType::Insert
    } else if trimmed.starts_with("UPDATE") {
        QueryType::Update
    } else if trimmed.starts_with("SELECT") {
        QueryType::Select
    } else {
        QueryType::Other
    }
}

pub fn format_stats_table(stats: &[InstanceStats]) -> String {
    let mut output = String::new();

    output.push_str(&"=".repeat(100));
    output.push('\n');
    output.push_str("PostgreSQL Proxy 统计信息\n");
    output.push_str(&"=".repeat(100));
    output.push('\n');

    for stat in stats {
        output.push_str(&format!("\n实例: {}\n", stat.name));
        output.push_str(&"-".repeat(60));
        output.push('\n');

        let conn = &stat.connections;
        let comp = &stat.compression;

        output.push_str(&format!("  运行时间:              {}\n", conn.format_uptime()));
        output.push_str(&format!(
            "  连接数:                总={} 活跃={}\n",
            conn.total_connections, conn.active_connections
        ));
        output.push_str(&format!(
            "  查询数:                总={} INSERT={} UPDATE={} SELECT={} 其他={}\n",
            conn.total_queries,
            conn.insert_queries,
            conn.update_queries,
            conn.select_queries,
            conn.other_queries
        ));
        output.push_str(&format!(
            "  QPS:                   {:.2}\n",
            conn.queries_per_second()
        ));

        let (to_client, from_client) = conn.throughput_mbps();
        output.push_str(&format!(
            "  吞吐量:                下行→客户端 {:.2} Mbps, 上行→服务器 {:.2} Mbps\n",
            to_client, from_client
        ));

        output.push_str(&format!(
            "  流量统计:              客户端→代理 {}  代理→服务器 {}  服务器→代理 {}  代理→客户端 {}\n",
            CompressionStats::format_size(conn.bytes_from_client),
            CompressionStats::format_size(conn.bytes_to_server),
            CompressionStats::format_size(conn.bytes_from_server),
            CompressionStats::format_size(conn.bytes_to_client)
        ));

        output.push_str(&format!(
            "  压缩统计:              压缩次数={} 解压次数={} 总跳过={}\n",
            comp.compress_count, comp.decompress_count, comp.total_skipped()
        ));
        output.push_str(&format!(
            "  跳过原因分布:          太小={} 低效={} 触发TOAST={}\n",
            comp.skipped_too_small, comp.skipped_inefficient, comp.skipped_toast
        ));
        output.push_str(&format!(
            "  压缩前大小:            {}\n",
            CompressionStats::format_size(comp.original_bytes)
        ));
        output.push_str(&format!(
            "  压缩后大小:            {}\n",
            CompressionStats::format_size(comp.compressed_bytes)
        ));
        output.push_str(&format!(
            "  压缩率:                {:.2}%\n",
            comp.compression_ratio() * 100.0
        ));
        output.push_str(&format!(
            "  节省空间:              {:.2}%\n",
            comp.space_saved_percent()
        ));
    }

    output.push_str(&"\n");
    output.push_str(&"=".repeat(100));
    output.push('\n');

    output
}
