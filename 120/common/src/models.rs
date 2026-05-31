use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Hash)]
pub enum Framework {
    Axum,
    ActixWeb,
    Rocket,
    Warp,
    Tide,
}

impl Framework {
    pub fn all() -> Vec<Framework> {
        vec![
            Framework::Axum,
            Framework::ActixWeb,
            Framework::Rocket,
            Framework::Warp,
            Framework::Tide,
        ]
    }

    pub fn name(&self) -> &'static str {
        match self {
            Framework::Axum => "Axum",
            Framework::ActixWeb => "Actix-web",
            Framework::Rocket => "Rocket",
            Framework::Warp => "Warp",
            Framework::Tide => "Tide",
        }
    }

    pub fn binary_name(&self) -> &'static str {
        match self {
            Framework::Axum => "axum-server",
            Framework::ActixWeb => "actix-server",
            Framework::Rocket => "rocket-server",
            Framework::Warp => "warp-server",
            Framework::Tide => "tide-server",
        }
    }

    pub fn port(&self) -> u16 {
        match self {
            Framework::Axum => 8001,
            Framework::ActixWeb => 8002,
            Framework::Rocket => 8003,
            Framework::Warp => 8004,
            Framework::Tide => 8005,
        }
    }

    pub fn from_name(name: &str) -> Option<Framework> {
        match name.to_lowercase().as_str() {
            "axum" => Some(Framework::Axum),
            "actix" | "actix-web" | "actixweb" => Some(Framework::ActixWeb),
            "rocket" => Some(Framework::Rocket),
            "warp" => Some(Framework::Warp),
            "tide" => Some(Framework::Tide),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Hash)]
pub enum Scenario {
    Json,
    DbQuery,
    Template,
    StaticFile,
    WebSocket,
}

impl Scenario {
    pub fn all() -> Vec<Scenario> {
        vec![
            Scenario::Json,
            Scenario::DbQuery,
            Scenario::Template,
            Scenario::StaticFile,
            Scenario::WebSocket,
        ]
    }

    pub fn name(&self) -> &'static str {
        match self {
            Scenario::Json => "JSON Serialization",
            Scenario::DbQuery => "Database Query",
            Scenario::Template => "Template Rendering",
            Scenario::StaticFile => "Static File",
            Scenario::WebSocket => "WebSocket Echo",
        }
    }

    pub fn path(&self) -> &'static str {
        match self {
            Scenario::Json => "/json",
            Scenario::DbQuery => "/db",
            Scenario::Template => "/template",
            Scenario::StaticFile => "/static/test.txt",
            Scenario::WebSocket => "/ws",
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BenchmarkConfig {
    pub concurrency: u32,
    pub duration_secs: u64,
    pub warmup_secs: u64,
    pub targets: Vec<Framework>,
    pub scenarios: Vec<Scenario>,
    pub repeat: u32,
    #[serde(default = "default_timeout")]
    pub timeout_secs: u64,
    #[serde(default)]
    pub debug_crash: bool,
    #[serde(default)]
    pub profile: ProfileConfig,
}

fn default_timeout() -> u64 {
    300
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProfileConfig {
    pub enabled: bool,
    pub sampling_freq_hz: u32,
    pub duration_secs: Option<u64>,
    pub generate_svg: bool,
    pub low_overhead_mode: bool,
}

impl Default for ProfileConfig {
    fn default() -> Self {
        ProfileConfig {
            enabled: false,
            sampling_freq_hz: 99,
            duration_secs: None,
            generate_svg: true,
            low_overhead_mode: true,
        }
    }
}

impl Default for BenchmarkConfig {
    fn default() -> Self {
        BenchmarkConfig {
            concurrency: 100,
            duration_secs: 60,
            warmup_secs: 5,
            targets: Framework::all(),
            scenarios: Scenario::all(),
            repeat: 3,
            timeout_secs: 300,
            debug_crash: false,
            profile: ProfileConfig::default(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ResourceUsage {
    pub cpu_percent: f32,
    pub memory_mb: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LatencyStats {
    pub p50_ms: f64,
    pub p95_ms: f64,
    pub p99_ms: f64,
    pub min_ms: f64,
    pub max_ms: f64,
    pub avg_ms: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ScenarioResult {
    pub scenario: Scenario,
    pub qps: f64,
    pub latency: LatencyStats,
    pub success_rate: f64,
    pub total_requests: u64,
    pub success_requests: u64,
    pub failed_requests: u64,
    pub avg_resource_usage: ResourceUsage,
    pub latency_distribution: Vec<(f64, u64)>,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
pub enum RunStatus {
    Success,
    Crash,
    Timeout,
    BuildFailed,
}

impl RunStatus {
    pub fn label(&self) -> &'static str {
        match self {
            RunStatus::Success => "Success",
            RunStatus::Crash => "Crash",
            RunStatus::Timeout => "Timeout",
            RunStatus::BuildFailed => "Build Failed",
        }
    }

    pub fn is_crash(&self) -> bool {
        matches!(self, RunStatus::Crash | RunStatus::Timeout)
    }
}

impl std::fmt::Display for RunStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.label())
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CrashInfo {
    pub reason: String,
    pub peak_memory_mb: f64,
    pub crash_log: Option<String>,
    pub timestamp: chrono::DateTime<chrono::Local>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FrameworkResult {
    pub framework: Framework,
    pub scenarios: HashMap<Scenario, ScenarioResult>,
    pub run_number: u32,
    pub status: RunStatus,
    pub crash_info: Option<CrashInfo>,
    #[serde(default)]
    pub profiles: HashMap<Scenario, crate::flamegraph::ProfileData>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BenchmarkReport {
    pub config: BenchmarkConfig,
    pub results: Vec<FrameworkResult>,
    pub timestamp: chrono::DateTime<chrono::Local>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AggregatedResult {
    pub framework: Framework,
    pub scenario: Scenario,
    pub qps_avg: f64,
    pub qps_std: f64,
    pub latency_avg: LatencyStats,
    pub success_rate_avg: f64,
    pub resource_avg: ResourceUsage,
    pub runs: u32,
}
