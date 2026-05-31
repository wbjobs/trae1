use crate::client::BenchmarkClient;
use common::flamegraph::{ProfileData, HotSpotAnalysis};
use common::models::{
    BenchmarkConfig, CrashInfo, Framework, FrameworkResult, RunStatus, Scenario, ScenarioResult,
};
use common::monitor::ResourceMonitor;
use common::process_manager::{ManagedChild, ProcessSupervisor};
use common::profiler::{analyze_hotspots, CpuProfiler};
use common::flamegraph_generator::{FlamegraphGenerator, compare_profiles};
use common::flamegraph::ProfileDiff;
use std::collections::HashMap;
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};
use tokio::time;

pub struct BenchmarkRunner {
    config: BenchmarkConfig,
    project_root: PathBuf,
    supervisor: ProcessSupervisor,
}

impl BenchmarkRunner {
    pub fn new(config: BenchmarkConfig, project_root: PathBuf) -> Self {
        let supervisor = ProcessSupervisor::new(config.timeout_secs, config.debug_crash);
        BenchmarkRunner {
            config,
            project_root,
            supervisor,
        }
    }

    fn build_server(&self, framework: Framework) -> Result<(), String> {
        let binary_name = framework.binary_name();
        println!("Building {}...", framework.name());

        let status = Command::new("cargo")
            .args(&[
                "build",
                "--release",
                "-p",
                binary_name,
            ])
            .current_dir(&self.project_root)
            .stdout(Stdio::inherit())
            .stderr(Stdio::inherit())
            .status()
            .map_err(|e| format!("Failed to start cargo build: {}", e))?;

        if !status.success() {
            return Err(format!("Failed to build {}", framework.name()));
        }

        Ok(())
    }

    async fn wait_for_server(&self, port: u16, timeout_secs: u64) -> Result<(), String> {
        let client = reqwest::Client::builder()
            .timeout(Duration::from_secs(1))
            .build()
            .unwrap();

        let start = Instant::now();
        let url = format!("http://127.0.0.1:{}/json", port);

        while start.elapsed().as_secs() < timeout_secs {
            match client.get(&url).send().await {
                Ok(resp) if resp.status().is_success() => {
                    println!("Server on port {} is ready!", port);
                    return Ok(());
                }
                _ => {
                    time::sleep(Duration::from_millis(200)).await;
                }
            }
        }

        Err(format!(
            "Server on port {} did not become ready within {} seconds",
            port, timeout_secs
        ))
    }

    async fn run_scenarios_with_profiling(
        &self,
        child: &mut ManagedChild,
        framework: Framework,
        _run_number: u32,
    ) -> Result<(HashMap<Scenario, ScenarioResult>, HashMap<Scenario, ProfileData>), String> {
        let base_url = format!("http://127.0.0.1:{}", framework.port());
        let client = BenchmarkClient::new(base_url);
        let pid = child.pid();

        let mut scenario_results: HashMap<Scenario, ScenarioResult> = HashMap::new();
        let mut profiles: HashMap<Scenario, ProfileData> = HashMap::new();
        let test_started = Instant::now();

        let profiling_enabled = self.config.profile.enabled;

        for scenario in &self.config.scenarios {
            if self.supervisor.check_timeout(test_started) {
                return Err(format!(
                    "Timeout: {} exceeded {}s total test time",
                    framework.name(),
                    self.config.timeout_secs
                ));
            }

            if !child.is_alive() {
                return Err(format!(
                    "Process for {} died during scenario: {}",
                    framework.name(),
                    scenario.name()
                ));
            }

            println!("  Testing: {}...", scenario.name());

            let duration_secs = self.config.profile.duration_secs.unwrap_or(self.config.duration_secs);
            let duration = Duration::from_secs(duration_secs);

            let mut profiler = CpuProfiler::new(
                common::models::ProfileConfig {
                    enabled: profiling_enabled,
                    sampling_freq_hz: self.config.profile.sampling_freq_hz,
                    duration_secs: Some(duration_secs),
                    generate_svg: self.config.profile.generate_svg,
                    low_overhead_mode: self.config.profile.low_overhead_mode,
                },
                self.project_root.clone(),
            );

            if profiling_enabled {
                if let Err(e) = profiler.start(pid, framework, *scenario) {
                    eprintln!("  Warning: Failed to start profiler: {}", e);
                }
            }

            let result = if *scenario == Scenario::WebSocket {
                let (result, _) = client
                    .run_websocket_benchmark(self.config.concurrency, duration, framework.port())
                    .await;
                result
            } else {
                let (result, _) = client
                    .run_benchmark(*scenario, self.config.concurrency, duration)
                    .await;
                result
            };

            if profiling_enabled {
                match profiler.stop() {
                    Ok(mut profile) => {
                        profile.framework = framework.name().to_string();
                        profile.scenario = scenario.name().to_string();
                        
                        if self.config.profile.generate_svg {
                            self.save_flamegraph(&profile, framework, *scenario);
                        }

                        let hotspots = analyze_hotspots(&profile);
                        self.print_hotspot_summary(&hotspots);
                        
                        profiles.insert(*scenario, profile);
                    }
                    Err(e) => {
                        eprintln!("  Warning: Profiler error: {}", e);
                    }
                }
            }

            println!(
                "    QPS: {:.0}, P50: {:.2}ms, P99: {:.2}ms, Success: {:.1}%",
                result.qps,
                result.latency.p50_ms,
                result.latency.p99_ms,
                result.success_rate * 100.0
            );

            child.update_peak_memory();

            if !child.is_alive() {
                return Err(format!(
                    "Process for {} died after scenario: {}",
                    framework.name(),
                    scenario.name()
                ));
            }

            scenario_results.insert(*scenario, result);

            time::sleep(Duration::from_secs(2)).await;
        }

        Ok((scenario_results, profiles))
    }

    fn save_flamegraph(&self, profile: &ProfileData, framework: Framework, scenario: Scenario) {
        let generator = FlamegraphGenerator::new();
        let svg = generator.generate_svg(profile);

        let flamegraph_dir = self.project_root.join("flamegraphs");
        if let Err(e) = std::fs::create_dir_all(&flamegraph_dir) {
            eprintln!("  Warning: Failed to create flamegraphs directory: {}", e);
            return;
        }

        let filename = format!(
            "flamegraph_{}_{}.svg",
            framework.name().to_lowercase().replace('-', "_"),
            scenario.name().to_lowercase().replace(' ', "_")
        );
        let filepath = flamegraph_dir.join(&filename);

        match std::fs::write(&filepath, &svg) {
            Ok(_) => println!("    🔥 Flamegraph saved to: flamegraphs/{}", filename),
            Err(e) => eprintln!("    Warning: Failed to save flamegraph: {}", e),
        }
    }

    fn print_hotspot_summary(&self, hotspots: &HotSpotAnalysis) {
        println!("    Top 5 Hotspots:");
        for (i, func) in hotspots.top_10_functions.iter().take(5).enumerate() {
            let category = common::flamegraph::categorize_function(&format!("{}::{}", func.module, func.name));
            println!(
                "      {}. {} - {:.2}% CPU [{}]",
                i + 1,
                self.truncate_name(&func.name, 50),
                func.cpu_percent,
                category
            );
        }

        if !hotspots.json_serialization_functions.is_empty() {
            let total_json: f64 = hotspots.json_serialization_functions.iter().map(|f| f.cpu_percent).sum();
            println!("    JSON Serialization overhead: {:.1}%", total_json);
        }
        if !hotspots.memory_alloc_functions.is_empty() {
            let total_alloc: f64 = hotspots.memory_alloc_functions.iter().map(|f| f.cpu_percent).sum();
            println!("    Memory Allocation overhead: {:.1}%", total_alloc);
        }
    }

    fn truncate_name(&self, name: &str, max_len: usize) -> String {
        if name.len() <= max_len {
            name.to_string()
        } else {
            let mut truncated: String = name.chars().take(max_len - 3).collect();
            truncated.push_str("...");
            truncated
        }
    }

    pub fn run_diff_analysis(&self, results: &[FrameworkResult]) -> Vec<ProfileDiff> {
        let mut diffs = Vec::new();
        
        let mut all_profiles: HashMap<(Framework, Scenario), &ProfileData> = HashMap::new();
        for result in results {
            if result.status == RunStatus::Success {
                for (scenario, profile) in &result.profiles {
                    all_profiles.insert((result.framework, *scenario), profile);
                }
            }
        }

        if self.config.targets.len() >= 2 {
            let a = self.config.targets[0];
            let b = self.config.targets[1];
            
            for scenario in &self.config.scenarios {
                if let (Some(profile_a), Some(profile_b)) = (
                    all_profiles.get(&(a, *scenario)),
                    all_profiles.get(&(b, *scenario)),
                ) {
                    println!("\n📊 Diff Analysis: {} vs {} - {}", a.name(), b.name(), scenario.name());
                    let diff = compare_profiles(profile_a, profile_b);
                    
                    println!("  Category Differences:");
                    let mut sorted_cats: Vec<(&String, &f64)> = diff.category_diffs.iter().collect();
                    sorted_cats.sort_by(|x, y| y.1.abs().partial_cmp(&x.1.abs()).unwrap());
                    for (cat, val) in sorted_cats.iter().take(5) {
                        let sign = if **val > 0.0 { "+" } else { "" };
                        println!("    {}: {}{:.2}%", cat, sign, val);
                    }
                    
                    self.save_diff_report(&diff, a, b, *scenario);
                    diffs.push(diff);
                }
            }
        }
        
        diffs
    }

    fn save_diff_report(&self, diff: &ProfileDiff, a: Framework, b: Framework, scenario: Scenario) {
        let report = common::flamegraph_generator::generate_diff_report(diff);
        
        let diff_dir = self.project_root.join("diff_reports");
        if let Err(e) = std::fs::create_dir_all(&diff_dir) {
            eprintln!("  Warning: Failed to create diff_reports directory: {}", e);
            return;
        }

        let filename = format!(
            "diff_{}_vs_{}_{}.md",
            a.name().to_lowercase().replace('-', "_"),
            b.name().to_lowercase().replace('-', "_"),
            scenario.name().to_lowercase().replace(' ', "_")
        );
        let filepath = diff_dir.join(&filename);

        match std::fs::write(&filepath, &report) {
            Ok(_) => println!("  📝 Diff report saved to: diff_reports/{}", filename),
            Err(e) => eprintln!("  Warning: Failed to save diff report: {}", e),
        }
    }

    pub async fn run_framework(
        &self,
        framework: Framework,
        run_number: u32,
    ) -> FrameworkResult {
        println!("\n--- {} (run {}/{}) ---", framework.name(), run_number, self.config.repeat);

        if let Err(e) = self.build_server(framework) {
            eprintln!("Build failed for {}: {}", framework.name(), e);
            return FrameworkResult {
                framework,
                scenarios: HashMap::new(),
                run_number,
                status: RunStatus::BuildFailed,
                crash_info: Some(CrashInfo {
                    reason: format!("Build failed: {}", e),
                    peak_memory_mb: 0.0,
                    crash_log: None,
                    timestamp: chrono::Local::now(),
                }),
                profiles: HashMap::new(),
            };
        }

        let mut child = match self.supervisor.spawn_monitored(framework, &self.project_root) {
            Ok(c) => c,
            Err(e) => {
                eprintln!("Failed to spawn {}: {}", framework.name(), e);
                return FrameworkResult {
                    framework,
                    scenarios: HashMap::new(),
                    run_number,
                    status: RunStatus::BuildFailed,
                    crash_info: Some(CrashInfo {
                        reason: format!("Spawn failed: {}", e),
                        peak_memory_mb: 0.0,
                        crash_log: None,
                        timestamp: chrono::Local::now(),
                    }),
                    profiles: HashMap::new(),
                };
            }
        };

        let framework_started = Instant::now();
        let pid = child.pid();
        let port = framework.port();

        let wait_result = self.wait_for_server(port, 30).await;
        if let Err(e) = wait_result {
            eprintln!("Server for {} failed to become ready: {}", framework.name(), e);
            let crash_info = child.cleanup();
            return FrameworkResult {
                framework,
                scenarios: HashMap::new(),
                run_number,
                status: RunStatus::Crash,
                crash_info: Some(crash_info),
                profiles: HashMap::new(),
            };
        }

        time::sleep(Duration::from_secs(self.config.warmup_secs)).await;

        let mut monitor = ResourceMonitor::new(pid);
        monitor.start(500);

        let scenario_results = self
            .run_scenarios_with_profiling(&mut child, framework, run_number)
            .await;

        monitor.stop();

        match scenario_results {
            Ok((results, profiles)) => {
                let mut scenarios_with_resources = HashMap::new();
                for (scenario, mut result) in results {
                    result.avg_resource_usage = monitor.get_average();
                    scenarios_with_resources.insert(scenario, result);
                }

                println!("Stopping {} server...", framework.name());
                if let Err(e) = child.kill() {
                    eprintln!("Warning: Failed to kill {}: {}", framework.name(), e);
                }

                FrameworkResult {
                    framework,
                    scenarios: scenarios_with_resources,
                    run_number,
                    status: RunStatus::Success,
                    crash_info: None,
                    profiles,
                }
            }
            Err(e) => {
                let status = if self.supervisor.check_timeout(framework_started) {
                    eprintln!(
                        "⏰ Timeout for {} after {}s: {}",
                        framework.name(),
                        self.config.timeout_secs,
                        e
                    );
                    RunStatus::Timeout
                } else {
                    eprintln!("💥 Crash for {}: {}", framework.name(), e);
                    RunStatus::Crash
                };

                child.update_peak_memory();
                let crash_info = child.cleanup();

                let mut crash_info = crash_info;
                crash_info.reason = format!("{}: {}", status.label(), e);

                if self.supervisor.debug_crash() {
                    let output_dir = self.project_root.join("crash_logs");
                    if let Err(io_err) = std::fs::create_dir_all(&output_dir) {
                        eprintln!("Warning: Failed to create crash_logs dir: {}", io_err);
                    } else if let Err(save_err) = common::process_manager::save_crash_log(
                        &crash_info,
                        &output_dir,
                    ) {
                        eprintln!("Warning: Failed to save crash log: {}", save_err);
                    } else {
                        let timestamp = crash_info
                            .timestamp
                            .format("%Y%m%d_%H%M%S");
                        println!(
                            "📝 Crash log saved to: crash_logs/crash_log_{}.log",
                            timestamp
                        );
                    }
                }

                FrameworkResult {
                    framework,
                    scenarios: HashMap::new(),
                    run_number,
                    status,
                    crash_info: Some(crash_info),
                    profiles: HashMap::new(),
                }
            }
        }
    }

    pub async fn run_all(&self) -> Result<Vec<FrameworkResult>, String> {
        let mut all_results = Vec::new();
        let mut crash_count = 0u32;
        let mut success_count = 0u32;

        for run in 1..=self.config.repeat {
            println!("\n========================================");
            println!("=== Run {}/{} ===", run, self.config.repeat);
            println!("========================================");

            for framework in &self.config.targets {
                let result = self.run_framework(*framework, run).await;

                if result.status.is_crash() {
                    crash_count += 1;
                    if let Some(info) = &result.crash_info {
                        eprintln!(
                            "  Status: {} | Reason: {} | Peak Memory: {:.1}MB",
                            result.status,
                            info.reason,
                            info.peak_memory_mb
                        );
                    }
                } else if result.status == RunStatus::Success {
                    success_count += 1;
                }

                all_results.push(result);
                time::sleep(Duration::from_secs(3)).await;
            }
        }

        if self.config.profile.enabled && self.config.targets.len() >= 2 {
            self.run_diff_analysis(&all_results);
        }

        println!("\n========================================");
        println!("Run Summary: {} success, {} crash/timeout", success_count, crash_count);
        println!("========================================");

        Ok(all_results)
    }
}
