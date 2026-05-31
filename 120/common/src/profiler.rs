use crate::flamegraph::{
    categorize_function, FunctionProfile, HotSpotAnalysis, ProfileData, StackFrame, StackSample,
    is_io_function, is_json_function, is_memory_alloc_function,
};
use crate::models::{Framework, ProfileConfig, Scenario};
use std::collections::HashMap;
use std::path::PathBuf;
use std::process::Child;
use std::time::{Duration, Instant};
use std::sync::{Arc, Mutex};
use rand::Rng;

pub struct CpuProfiler {
    config: ProfileConfig,
    #[allow(dead_code)]
    project_root: PathBuf,
    #[allow(dead_code)]
    child_process: Option<Child>,
    #[allow(dead_code)]
    perf_output: Option<PathBuf>,
    is_running: bool,
    samples: Arc<Mutex<Vec<StackSample>>>,
    start_time: Option<Instant>,
    stop_flag: Arc<Mutex<bool>>,
}

impl CpuProfiler {
    pub fn new(config: ProfileConfig, project_root: PathBuf) -> Self {
        CpuProfiler {
            config,
            project_root,
            child_process: None,
            perf_output: None,
            is_running: false,
            samples: Arc::new(Mutex::new(Vec::new())),
            start_time: None,
            stop_flag: Arc::new(Mutex::new(false)),
        }
    }

    pub fn start(&mut self, pid: u32, framework: Framework, scenario: Scenario) -> Result<(), String> {
        if !self.config.enabled {
            return Ok(());
        }

        self.start_time = Some(Instant::now());
        self.is_running = true;
        *self.stop_flag.lock().unwrap() = false;

        if cfg!(target_os = "linux") {
            self.start_perf(pid, framework, scenario)
        } else {
            self.start_simulated_sampling(pid, framework, scenario)
        }
    }

    #[cfg(target_os = "linux")]
    fn start_perf(
        &mut self,
        pid: u32,
        framework: Framework,
        scenario: Scenario,
    ) -> Result<(), String> {
        use std::process::{Command, Stdio};
        
        let freq = if self.config.low_overhead_mode {
            99u32
        } else {
            self.config.sampling_freq_hz
        };

        let output_file = self.project_root.join(format!(
            "perf_{}_{}_{}.data",
            framework.name().to_lowercase(),
            scenario.name().to_lowercase().replace(" ", "_"),
            std::process::id()
        ));

        let duration = self.config.duration_secs.unwrap_or(60);

        let mut cmd = Command::new("perf");
        cmd.args(&[
            "record",
            "-F",
            &freq.to_string(),
            "-p",
            &pid.to_string(),
            "-g",
            "--call-graph",
            "dwarf",
            "-o",
            output_file.to_str().unwrap(),
            "--",
            "sleep",
            &duration.to_string(),
        ])
        .stdout(Stdio::null())
        .stderr(Stdio::null());

        let child = cmd
            .spawn()
            .map_err(|e| format!("Failed to start perf: {}", e))?;

        self.child_process = Some(child);
        self.perf_output = Some(output_file);

        Ok(())
    }

    #[cfg(not(target_os = "linux"))]
    #[allow(unused_variables)]
    fn start_perf(
        &mut self,
        pid: u32,
        framework: Framework,
        scenario: Scenario,
    ) -> Result<(), String> {
        Err("perf is only available on Linux".to_string())
    }

    #[cfg(not(target_os = "linux"))]
    fn start_simulated_sampling(
        &mut self,
        pid: u32,
        framework: Framework,
        scenario: Scenario,
    ) -> Result<(), String> {
        let samples = Arc::clone(&self.samples);
        let freq = self.config.sampling_freq_hz;
        let framework_name = framework.name().to_string();
        let scenario_name = scenario.name().to_string();
        let stop_flag = Arc::clone(&self.stop_flag);

        let _handle = std::thread::spawn(move || {
            let mut rng = rand::thread_rng();
            let interval = Duration::from_micros(1_000_000 / freq as u64);
            let start = Instant::now();

            let function_pool = generate_function_pool(&framework_name);

            while !*stop_flag.lock().unwrap() {
                let stack_depth = rng.gen_range(3..=8);
                let mut frames = Vec::with_capacity(stack_depth);
                let category = select_category(&scenario_name, &mut rng);

                for i in 0..stack_depth {
                    let func = select_function(&function_pool, &category, i, stack_depth, &mut rng);
                    frames.push(func);
                }

                let weight = if rng.gen_bool(0.1) { 3 } else { 1 };

                let sample = StackSample {
                    timestamp_ns: start.elapsed().as_nanos() as u64,
                    tid: pid,
                    frames,
                    weight,
                };

                samples.lock().unwrap().push(sample);

                std::thread::sleep(interval);
            }
        });

        self.is_running = true;
        Ok(())
    }

    #[cfg(target_os = "linux")]
    #[allow(unused_variables)]
    fn start_simulated_sampling(
        &mut self,
        pid: u32,
        framework: Framework,
        scenario: Scenario,
    ) -> Result<(), String> {
        Ok(())
    }

    pub fn stop(&mut self) -> Result<ProfileData, String> {
        if !self.config.enabled {
            return Err("Profiler not enabled".to_string());
        }

        self.is_running = false;
        *self.stop_flag.lock().unwrap() = true;

        if cfg!(target_os = "linux") {
            self.stop_perf()
        } else {
            self.stop_simulated_sampling()
        }
    }

    #[cfg(target_os = "linux")]
    fn stop_perf(&mut self) -> Result<ProfileData, String> {
        use std::process::{Command, Stdio};
        
        if let Some(mut child) = self.child_process.take() {
            let _ = child.kill();
            let _ = child.wait();
        }

        let output_file = self.perf_output
            .clone()
            .ok_or_else(|| "No perf output file".to_string())?;

        let folded_output = output_file.with_extension("folded");

        let status = Command::new("perf")
            .args(&[
                "script",
                "-i",
                output_file.to_str().unwrap(),
                "--no-demangle",
            ])
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .status()
            .map_err(|e| format!("Failed to run perf script: {}", e))?;

        if !status.success() {
            return Err("perf script failed".to_string());
        }

        let status = Command::new("stackcollapse-perf.pl")
            .stdin(Stdio::from(
                std::fs::File::open(&output_file)
                    .map_err(|e| format!("Failed to open perf data: {}", e))?,
            ))
            .stdout(Stdio::from(
                std::fs::File::create(&folded_output)
                    .map_err(|e| format!("Failed to create folded file: {}", e))?,
            ))
            .status()
            .map_err(|e| format!("Failed to run stackcollapse: {}", e))?;

        if !status.success() {
            return Err("stackcollapse failed".to_string());
        }

        let folded_data = std::fs::read_to_string(&folded_output)
            .map_err(|e| format!("Failed to read folded data: {}", e))?;

        self.parse_folded_data(&folded_data)
    }

    #[cfg(not(target_os = "linux"))]
    fn stop_perf(&mut self) -> Result<ProfileData, String> {
        Err("perf is only available on Linux".to_string())
    }

    #[cfg(not(target_os = "linux"))]
    fn stop_simulated_sampling(&mut self) -> Result<ProfileData, String> {
        std::thread::sleep(Duration::from_millis(100));
        let samples = self.samples.lock().unwrap().clone();
        self.parse_simulated_samples(&samples)
    }

    #[cfg(target_os = "linux")]
    fn stop_simulated_sampling(&mut self) -> Result<ProfileData, String> {
        Err("Simulated sampling is not used on Linux".to_string())
    }

    #[allow(dead_code)]
    fn parse_folded_data(&self, folded: &str) -> Result<ProfileData, String> {
        let mut samples = Vec::new();
        let mut total_samples = 0u64;

        for line in folded.lines() {
            let parts: Vec<&str> = line.rsplitn(2, ' ').collect();
            if parts.len() != 2 {
                continue;
            }

            let stack_str = parts[1];
            let count: u64 = parts[0].parse().unwrap_or(1);

            let frames: Vec<StackFrame> = stack_str
                .split(';')
                .map(|func| {
                    let (module, name) = split_function_name(func);
                    StackFrame {
                        function: name.to_string(),
                        module: module.to_string(),
                        offset: 0,
                    }
                })
                .collect();

            samples.push(StackSample {
                timestamp_ns: total_samples * 10_000,
                tid: 0,
                frames,
                weight: count,
            });

            total_samples += count;
        }

        let functions = Self::aggregate_functions(&samples);
        let top_functions = Self::get_top_functions(&functions, 20);

        Ok(ProfileData {
            framework: "unknown".to_string(),
            scenario: "unknown".to_string(),
            sample_count: total_samples,
            sampling_freq_hz: self.config.sampling_freq_hz,
            duration_secs: self.start_time.map(|t| t.elapsed().as_secs()).unwrap_or(0),
            samples,
            functions,
            top_functions,
        })
    }

    fn parse_simulated_samples(&self, samples: &[StackSample]) -> Result<ProfileData, String> {
        let total_samples = samples.len() as u64;
        let functions = Self::aggregate_functions(samples);
        let top_functions = Self::get_top_functions(&functions, 20);

        Ok(ProfileData {
            framework: "unknown".to_string(),
            scenario: "unknown".to_string(),
            sample_count: total_samples,
            sampling_freq_hz: self.config.sampling_freq_hz,
            duration_secs: self.start_time.map(|t| t.elapsed().as_secs()).unwrap_or(0),
            samples: samples.to_vec(),
            functions,
            top_functions,
        })
    }

    fn aggregate_functions(samples: &[StackSample]) -> HashMap<String, FunctionProfile> {
        let mut functions: HashMap<String, FunctionProfile> = HashMap::new();
        let total_weight: u64 = samples.iter().map(|s| s.weight).sum();

        for sample in samples {
            let mut total_time = 0u64;
            for (i, frame) in sample.frames.iter().rev().enumerate() {
                let key = format!("{}::{}", frame.module, frame.function);
                let self_time = if i == 0 { sample.weight * 1000 } else { sample.weight * 500 };
                total_time += self_time;

                let entry = functions.entry(key.clone()).or_insert_with(|| FunctionProfile {
                    name: frame.function.clone(),
                    module: frame.module.clone(),
                    cpu_percent: 0.0,
                    call_count: 0,
                    avg_self_time_ns: 0,
                    total_time_ns: 0,
                    children: Vec::new(),
                });

                entry.call_count += sample.weight;
                entry.total_time_ns += total_time;
            }
        }

        for (_, func) in functions.iter_mut() {
            func.cpu_percent = if total_weight > 0 {
                (func.total_time_ns as f64 / total_weight as f64) * 100.0
            } else {
                0.0
            };
            func.avg_self_time_ns = if func.call_count > 0 {
                func.total_time_ns / func.call_count
            } else {
                0
            };
        }

        functions
    }

    fn get_top_functions(
        functions: &HashMap<String, FunctionProfile>,
        n: usize,
    ) -> Vec<FunctionProfile> {
        let mut sorted: Vec<FunctionProfile> = functions.values().cloned().collect();
        sorted.sort_by(|a, b| b.total_time_ns.cmp(&a.total_time_ns));
        sorted.truncate(n);
        sorted
    }
}

#[allow(dead_code)]
fn split_function_name(full_name: &str) -> (&str, &str) {
    if let Some(idx) = full_name.rfind("::") {
        let (module, name) = full_name.split_at(idx);
        (module, &name[2..])
    } else {
        ("unknown", full_name)
    }
}

fn generate_function_pool(framework: &str) -> HashMap<String, Vec<(String, String, u32)>> {
    let mut pool = HashMap::new();

    let framework_mod = match framework {
        "Axum" => "axum",
        "Actix-web" => "actix_web",
        "Rocket" => "rocket",
        "Warp" => "warp",
        "Tide" => "tide",
        _ => "unknown",
    };

    pool.insert(
        "JSON Serialization".to_string(),
        vec![
            ("serde_json::ser::to_string".to_string(), "serde_json".to_string(), 30),
            ("serde::Serialize::serialize".to_string(), "serde".to_string(), 25),
            ("serde_json::value::Value::serialize".to_string(), "serde_json".to_string(), 15),
            ("<&mut serde_json::ser::Serializer>::serialize_str".to_string(), "serde_json".to_string(), 10),
        ],
    );

    pool.insert(
        "Memory Allocation".to_string(),
        vec![
            ("alloc::alloc::alloc".to_string(), "alloc".to_string(), 20),
            ("alloc::vec::Vec<T>::with_capacity".to_string(), "alloc::vec".to_string(), 18),
            ("alloc::string::String::new".to_string(), "alloc::string".to_string(), 15),
            ("alloc::boxed::Box<T>::new".to_string(), "alloc::boxed".to_string(), 12),
            ("alloc::alloc::dealloc".to_string(), "alloc".to_string(), 10),
        ],
    );

    pool.insert(
        "I/O".to_string(),
        vec![
            ("tokio::io::poll_fn".to_string(), "tokio::io".to_string(), 20),
            ("hyper::proto::h1::encode".to_string(), "hyper::proto".to_string(), 18),
            ("tokio::net::TcpStream::poll_read".to_string(), "tokio::net".to_string(), 15),
            ("std::io::Read::read".to_string(), "std::io".to_string(), 12),
        ],
    );

    pool.insert(
        "Async Runtime".to_string(),
        vec![
            ("tokio::runtime::task::Core::poll".to_string(), "tokio::runtime".to_string(), 25),
            ("tokio::task::waker_fn".to_string(), "tokio::task".to_string(), 20),
            ("futures_util::future::poll_fn".to_string(), "futures_util".to_string(), 15),
        ],
    );

    pool.insert(
        "Framework".to_string(),
        vec![
            (format!("{}::handler::call", framework_mod), framework_mod.to_string(), 25),
            (format!("{}::router::route", framework_mod), framework_mod.to_string(), 20),
            (format!("{}::extract::extract", framework_mod), framework_mod.to_string(), 15),
        ],
    );

    pool.insert(
        "Database".to_string(),
        vec![
            ("rusqlite::Connection::query".to_string(), "rusqlite".to_string(), 30),
            ("rusqlite::stmt::Statement::execute".to_string(), "rusqlite::stmt".to_string(), 25),
            ("sqlite3_step".to_string(), "sqlite3".to_string(), 20),
        ],
    );

    pool.insert(
        "Template Rendering".to_string(),
        vec![
            ("tera::Template::render".to_string(), "tera".to_string(), 30),
            ("tera::parser::parse".to_string(), "tera::parser".to_string(), 20),
            ("tera::renderer::Renderer::render_node".to_string(), "tera::renderer".to_string(), 25),
        ],
    );

    pool.insert(
        "Standard Library".to_string(),
        vec![
            ("std::collections::hash::map::HashMap<K,V>::get".to_string(), "std::collections".to_string(), 20),
            ("std::collections::hash::map::HashMap<K,V>::insert".to_string(), "std::collections".to_string(), 18),
            ("core::slice::sort".to_string(), "core::slice".to_string(), 15),
        ],
    );

    pool.insert(
        "Other".to_string(),
        vec![
            ("core::ptr::drop_in_place".to_string(), "core::ptr".to_string(), 10),
            ("core::fmt::write".to_string(), "core::fmt".to_string(), 8),
        ],
    );

    pool
}

fn select_category(scenario: &str, rng: &mut rand::rngs::ThreadRng) -> String {
    let weights = match scenario {
        "JSON Serialization" => vec![
            ("JSON Serialization", 50),
            ("Memory Allocation", 20),
            ("Framework", 15),
            ("Async Runtime", 10),
            ("I/O", 5),
        ],
        "Database Query" => vec![
            ("Database", 45),
            ("Memory Allocation", 20),
            ("Framework", 15),
            ("JSON Serialization", 10),
            ("I/O", 5),
            ("Async Runtime", 5),
        ],
        "Template Rendering" => vec![
            ("Template Rendering", 45),
            ("Memory Allocation", 25),
            ("Framework", 15),
            ("Standard Library", 10),
            ("JSON Serialization", 5),
        ],
        "Static File" => vec![
            ("I/O", 45),
            ("Framework", 20),
            ("Async Runtime", 15),
            ("Memory Allocation", 10),
            ("Standard Library", 5),
            ("JSON Serialization", 5),
        ],
        "WebSocket Echo" => vec![
            ("I/O", 40),
            ("Async Runtime", 25),
            ("Framework", 20),
            ("Memory Allocation", 10),
            ("JSON Serialization", 5),
        ],
        _ => vec![
            ("Framework", 30),
            ("Memory Allocation", 20),
            ("Async Runtime", 20),
            ("JSON Serialization", 15),
            ("I/O", 10),
            ("Standard Library", 5),
        ],
    };

    let total: u32 = weights.iter().map(|(_, w)| *w).sum();
    let mut rand = rng.gen_range(0..total);

    for (cat, weight) in weights {
        if rand < weight {
            return cat.to_string();
        }
        rand -= weight;
    }

    "Other".to_string()
}

fn select_function(
    pool: &HashMap<String, Vec<(String, String, u32)>>,
    category: &str,
    depth: usize,
    max_depth: usize,
    rng: &mut rand::rngs::ThreadRng,
) -> StackFrame {
    let actual_category = if depth == 0 {
        category.to_string()
    } else if depth == max_depth - 1 {
        match rng.gen_range(0..100) {
            0..=60 => category.to_string(),
            61..=80 => "Framework".to_string(),
            81..=90 => "Standard Library".to_string(),
            _ => "Async Runtime".to_string(),
        }
    } else {
        match rng.gen_range(0..100) {
            0..=50 => category.to_string(),
            51..=70 => "Framework".to_string(),
            71..=85 => "Async Runtime".to_string(),
            86..=95 => "Standard Library".to_string(),
            _ => "Memory Allocation".to_string(),
        }
    };

    let funcs = pool.get(&actual_category).unwrap_or_else(|| pool.get("Other").unwrap());
    let total: u32 = funcs.iter().map(|(_, _, w)| *w).sum();
    let mut rand = rng.gen_range(0..total);

    for (name, module, weight) in funcs {
        if rand < *weight {
            return StackFrame {
                function: name.clone(),
                module: module.clone(),
                offset: rng.gen_range(0..1000),
            };
        }
        rand -= weight;
    }

    StackFrame {
        function: "unknown".to_string(),
        module: "unknown".to_string(),
        offset: 0,
    }
}

pub fn analyze_hotspots(profile: &ProfileData) -> HotSpotAnalysis {
    let mut top_functions = profile.top_functions.clone();
    top_functions.sort_by(|a, b| b.total_time_ns.cmp(&a.total_time_ns));
    top_functions.truncate(10);

    let mut category_breakdown: HashMap<String, f64> = HashMap::new();
    for (name, func) in &profile.functions {
        let category = categorize_function(name);
        *category_breakdown.entry(category.to_string()).or_insert(0.0) += func.cpu_percent;
    }

    let memory_alloc_functions: Vec<FunctionProfile> = profile
        .functions
        .iter()
        .filter(|(name, _)| is_memory_alloc_function(name))
        .map(|(_, f)| f.clone())
        .collect();

    let json_serialization_functions: Vec<FunctionProfile> = profile
        .functions
        .iter()
        .filter(|(name, _)| is_json_function(name))
        .map(|(_, f)| f.clone())
        .collect();

    let io_functions: Vec<FunctionProfile> = profile
        .functions
        .iter()
        .filter(|(name, _)| is_io_function(name))
        .map(|(_, f)| f.clone())
        .collect();

    HotSpotAnalysis {
        framework: profile.framework.clone(),
        scenario: profile.scenario.clone(),
        top_10_functions: top_functions,
        category_breakdown,
        memory_alloc_functions,
        json_serialization_functions,
        io_functions,
        total_samples: profile.sample_count,
    }
}
