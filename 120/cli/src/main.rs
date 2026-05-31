use clap::{Parser, ValueEnum};
use common::models::{BenchmarkConfig, BenchmarkReport, Framework, Scenario};
use common::stats::aggregate_results;
use common::report::{generate_html_report, generate_markdown_report};
use benches::runner::BenchmarkRunner;
use std::fmt;
use std::path::PathBuf;
use std::str::FromStr;

#[derive(Parser, Debug)]
#[command(author, version, about = "Rust Web Framework Benchmark Tool", long_about = None)]
struct Cli {
    #[arg(short, long, value_parser = parse_concurrency, default_value_t = 100)]
    concurrency: u32,

    #[arg(short, long, default_value_t = 60)]
    duration: u64,

    #[arg(long, default_value_t = 5)]
    warmup: u64,

    #[arg(long, value_delimiter = ',', default_values_t = FrameworkCli::all_values())]
    target: Vec<FrameworkCli>,

    #[arg(long, value_delimiter = ',', default_values_t = ScenarioCli::all_values())]
    scenarios: Vec<ScenarioCli>,

    #[arg(short, long, default_value_t = 3)]
    repeat: u32,

    #[arg(long, default_value = "benchmark_report")]
    output: String,

    #[arg(long, default_value = "./")]
    project_root: PathBuf,

    #[arg(long, default_value_t = 300, help = "Timeout per framework test in seconds")]
    timeout: u64,

    #[arg(long, help = "Save full crash logs for debugging")]
    debug_crash: bool,

    #[arg(long, help = "Enable CPU profiling and flamegraph generation")]
    flamegraph: bool,

    #[arg(long, value_delimiter = ',', help = "Specific frameworks to profile (e.g. axum,actix-web)")]
    flamegraph_targets: Option<Vec<String>>,

    #[arg(long, default_value_t = 99, help = "Sampling frequency in Hz (default 99 for low overhead)")]
    flamegraph_freq: u32,

    #[arg(long, help = "Duration of profiling per scenario in seconds")]
    flamegraph_duration: Option<u64>,

    #[arg(long, value_delimiter = ',', help = "Compare two frameworks' flamegraphs (e.g. axum,actix-web)")]
    diff: Option<Vec<String>>,

    #[arg(long, help = "Disable low overhead mode (higher sampling rate but more overhead)")]
    high_overhead: bool,
}

#[derive(Copy, Clone, Debug, ValueEnum, PartialEq, Eq)]
enum FrameworkCli {
    Axum,
    ActixWeb,
    Rocket,
    Warp,
    Tide,
}

impl FrameworkCli {
    fn all_values() -> Vec<Self> {
        vec![
            FrameworkCli::Axum,
            FrameworkCli::ActixWeb,
            FrameworkCli::Rocket,
            FrameworkCli::Warp,
            FrameworkCli::Tide,
        ]
    }

    fn to_model(self) -> Framework {
        match self {
            FrameworkCli::Axum => Framework::Axum,
            FrameworkCli::ActixWeb => Framework::ActixWeb,
            FrameworkCli::Rocket => Framework::Rocket,
            FrameworkCli::Warp => Framework::Warp,
            FrameworkCli::Tide => Framework::Tide,
        }
    }
}

impl fmt::Display for FrameworkCli {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FrameworkCli::Axum => write!(f, "axum"),
            FrameworkCli::ActixWeb => write!(f, "actix-web"),
            FrameworkCli::Rocket => write!(f, "rocket"),
            FrameworkCli::Warp => write!(f, "warp"),
            FrameworkCli::Tide => write!(f, "tide"),
        }
    }
}

impl FromStr for FrameworkCli {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "axum" => Ok(FrameworkCli::Axum),
            "actix" | "actix-web" | "actixweb" => Ok(FrameworkCli::ActixWeb),
            "rocket" => Ok(FrameworkCli::Rocket),
            "warp" => Ok(FrameworkCli::Warp),
            "tide" => Ok(FrameworkCli::Tide),
            _ => Err(format!("Unknown framework: {}", s)),
        }
    }
}

#[derive(Copy, Clone, Debug, ValueEnum, PartialEq, Eq)]
enum ScenarioCli {
    Json,
    DbQuery,
    Template,
    StaticFile,
    WebSocket,
}

impl ScenarioCli {
    fn all_values() -> Vec<Self> {
        vec![
            ScenarioCli::Json,
            ScenarioCli::DbQuery,
            ScenarioCli::Template,
            ScenarioCli::StaticFile,
            ScenarioCli::WebSocket,
        ]
    }

    fn to_model(self) -> Scenario {
        match self {
            ScenarioCli::Json => Scenario::Json,
            ScenarioCli::DbQuery => Scenario::DbQuery,
            ScenarioCli::Template => Scenario::Template,
            ScenarioCli::StaticFile => Scenario::StaticFile,
            ScenarioCli::WebSocket => Scenario::WebSocket,
        }
    }
}

impl fmt::Display for ScenarioCli {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ScenarioCli::Json => write!(f, "json"),
            ScenarioCli::DbQuery => write!(f, "db-query"),
            ScenarioCli::Template => write!(f, "template"),
            ScenarioCli::StaticFile => write!(f, "static-file"),
            ScenarioCli::WebSocket => write!(f, "websocket"),
        }
    }
}

impl FromStr for ScenarioCli {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "json" => Ok(ScenarioCli::Json),
            "db" | "dbquery" | "db-query" => Ok(ScenarioCli::DbQuery),
            "template" | "tera" => Ok(ScenarioCli::Template),
            "static" | "staticfile" | "static-file" => Ok(ScenarioCli::StaticFile),
            "ws" | "websocket" => Ok(ScenarioCli::WebSocket),
            _ => Err(format!("Unknown scenario: {}", s)),
        }
    }
}

fn parse_concurrency(s: &str) -> Result<u32, String> {
    let concurrency: u32 = s.parse().map_err(|_| format!("Invalid concurrency: {}", s))?;
    if concurrency < 1 || concurrency > 10000 {
        return Err(format!("Concurrency must be between 1 and 10000, got {}", concurrency));
    }
    Ok(concurrency)
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cli = Cli::parse();

    let targets: Vec<Framework> = cli.target.iter().map(|f| f.to_model()).collect();
    let scenarios: Vec<Scenario> = cli.scenarios.iter().map(|s| s.to_model()).collect();

    let profile_targets = cli.flamegraph_targets.as_ref().map(|names| {
        names.iter().filter_map(|n| Framework::from_name(n)).collect::<Vec<_>>()
    });

    let profile_enabled = cli.flamegraph || cli.diff.is_some();
    let config = BenchmarkConfig {
        concurrency: cli.concurrency,
        duration_secs: cli.duration,
        warmup_secs: cli.warmup,
        targets: targets.clone(),
        scenarios: scenarios.clone(),
        repeat: cli.repeat,
        timeout_secs: cli.timeout,
        debug_crash: cli.debug_crash,
        profile: common::models::ProfileConfig {
            enabled: profile_enabled,
            sampling_freq_hz: cli.flamegraph_freq,
            duration_secs: cli.flamegraph_duration,
            generate_svg: true,
            low_overhead_mode: !cli.high_overhead,
        },
    };

    println!("🦀 Rust Web Framework Benchmark");
    println!("================================");
    println!("Configuration:");
    println!("  Concurrency: {}", config.concurrency);
    println!("  Duration: {}s", config.duration_secs);
    println!("  Warmup: {}s", config.warmup_secs);
    println!("  Repeat: {}x", config.repeat);
    println!("  Timeout: {}s", config.timeout_secs);
    if config.debug_crash {
        println!("  Debug Crash: enabled");
    }
    if config.profile.enabled {
        println!("  CPU Profiling: enabled");
        println!("  Sampling Frequency: {} Hz", config.profile.sampling_freq_hz);
        println!("  Low Overhead Mode: {}", if config.profile.low_overhead_mode { "yes" } else { "no" });
        if let Some(duration) = config.profile.duration_secs {
            println!("  Profile Duration: {}s", duration);
        }
        if let Some(ref targets) = profile_targets {
            println!("  Profile Targets: {}", targets.iter().map(|f| f.name()).collect::<Vec<_>>().join(", "));
        }
    }
    if let Some(ref diff) = cli.diff {
        println!("  Diff Mode: {} vs {}", diff[0], diff[1]);
    }
    println!("  Frameworks: {}", targets.iter().map(|f| f.name()).collect::<Vec<_>>().join(", "));
    println!("  Scenarios: {}", scenarios.iter().map(|s| s.name()).collect::<Vec<_>>().join(", "));
    println!();

    let project_root = std::fs::canonicalize(&cli.project_root)?;
    println!("Project root: {}", project_root.display());

    let runner = BenchmarkRunner::new(config.clone(), project_root);
    let results = runner.run_all().await?;

    println!("\n========================================");
    println!("Benchmark complete! Generating reports...");
    println!("========================================");

    let timestamp = chrono::Local::now();
    let report = BenchmarkReport {
        config: config.clone(),
        results: results.clone(),
        timestamp,
    };

    let aggregated = aggregate_results(&results);

    let html_report = generate_html_report(&report, &aggregated);
    let md_report = generate_markdown_report(&report, &aggregated);

    let html_path = format!("{}.html", cli.output);
    let md_path = format!("{}.md", cli.output);
    let json_path = format!("{}.json", cli.output);

    std::fs::write(&html_path, html_report)?;
    println!("✓ HTML report written to: {}", html_path);

    std::fs::write(&md_path, md_report)?;
    println!("✓ Markdown report written to: {}", md_path);

    let json_data = serde_json::to_string_pretty(&report)?;
    std::fs::write(&json_path, json_data)?;
    println!("✓ Raw JSON data written to: {}", json_path);

    println!("\nSummary:");
    for scenario in &scenarios {
        println!("\n📊 {}:", scenario.name());
        let mut scenario_agg: Vec<_> = aggregated.iter().filter(|a| a.scenario == *scenario).collect();
        scenario_agg.sort_by(|a, b| b.qps_avg.partial_cmp(&a.qps_avg).unwrap());
        
        for (i, agg) in scenario_agg.iter().enumerate() {
            println!("  {}. {} - QPS: {:.0} ± {:.0}, P99: {:.2}ms",
                i + 1,
                agg.framework.name(),
                agg.qps_avg,
                agg.qps_std,
                agg.latency_avg.p99_ms
            );
        }
    }

    let crash_results: Vec<_> = results.iter().filter(|r| r.status != common::models::RunStatus::Success).collect();
    if !crash_results.is_empty() {
        println!("\n⚠️  Crash/Timeout Summary:");
        for r in &crash_results {
            if let Some(info) = &r.crash_info {
                println!("  {} (run {}): {} | Peak Memory: {:.1}MB",
                    r.framework.name(),
                    r.run_number,
                    info.reason,
                    info.peak_memory_mb
                );
            } else {
                println!("  {} (run {}): {}",
                    r.framework.name(),
                    r.run_number,
                    r.status
                );
            }
        }
    }

    Ok(())
}
