use crate::models::{CrashInfo, Framework, RunStatus};
use std::io::{BufRead, BufReader};
use std::path::PathBuf;
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};
use sysinfo::{Pid, System};

pub struct ManagedChild {
    child: Child,
    pid: u32,
    framework: Framework,
    stdout_rx: Option<std::sync::mpsc::Receiver<String>>,
    stderr_rx: Option<std::sync::mpsc::Receiver<String>>,
    stdout_handle: Option<thread::JoinHandle<()>>,
    stderr_handle: Option<thread::JoinHandle<()>>,
    log_buffer: Arc<Mutex<Vec<String>>>,
    started_at: Instant,
    peak_memory: Arc<Mutex<f64>>,
    sys: Arc<Mutex<System>>,
    debug_crash: bool,
    heartbeat_alive: Arc<Mutex<bool>>,
}

impl ManagedChild {
    pub fn spawn(
        framework: Framework,
        project_root: &PathBuf,
        debug_crash: bool,
    ) -> Result<Self, String> {
        let binary_name = framework.binary_name();
        let binary_path = if cfg!(target_os = "windows") {
            project_root
                .join("target")
                .join("release")
                .join(format!("{}.exe", binary_name))
        } else {
            project_root
                .join("target")
                .join("release")
                .join(binary_name)
        };

        if !binary_path.exists() {
            return Err(format!(
                "Binary not found: {}. Run cargo build --release -p {} first.",
                binary_path.display(),
                binary_name
            ));
        }

        let mut cmd = Command::new(&binary_path);
        cmd.stdout(Stdio::piped());
        cmd.stderr(Stdio::piped());

        let mut child = cmd
            .spawn()
            .map_err(|e| format!("Failed to start {}: {}", framework.name(), e))?;

        let pid = child.id();

        let stdout = child.stdout.take();
        let stderr = child.stderr.take();

        let (stdout_tx, stdout_rx) = std::sync::mpsc::channel::<String>();
        let (stderr_tx, stderr_rx) = std::sync::mpsc::channel::<String>();

        let log_buffer = Arc::new(Mutex::new(Vec::<String>::new()));

        let stdout_buffer = Arc::clone(&log_buffer);
        let stdout_handle = stdout.map(|stream| {
            thread::spawn(move || {
                let reader = BufReader::new(stream);
                for line in reader.lines() {
                    if let Ok(line) = line {
                        let _ = stdout_tx.send(line.clone());
                        let mut buf = stdout_buffer.lock().unwrap();
                        buf.push(format!("[stdout] {}", line));
                        if buf.len() > 10000 {
                            let drain_count = buf.len() - 5000;
                            buf.drain(0..drain_count);
                        }
                    }
                }
            })
        });

        let stderr_buffer = Arc::clone(&log_buffer);
        let stderr_handle = stderr.map(|stream| {
            thread::spawn(move || {
                let reader = BufReader::new(stream);
                for line in reader.lines() {
                    if let Ok(line) = line {
                        let _ = stderr_tx.send(line.clone());
                        let mut buf = stderr_buffer.lock().unwrap();
                        buf.push(format!("[stderr] {}", line));
                        if buf.len() > 10000 {
                            let drain_count = buf.len() - 5000;
                            buf.drain(0..drain_count);
                        }
                    }
                }
            })
        });

        let mut sys = System::new_all();
        sys.refresh_all();

        Ok(ManagedChild {
            child,
            pid,
            framework,
            stdout_rx: Some(stdout_rx),
            stderr_rx: Some(stderr_rx),
            stdout_handle,
            stderr_handle,
            log_buffer,
            started_at: Instant::now(),
            peak_memory: Arc::new(Mutex::new(0.0)),
            sys: Arc::new(Mutex::new(sys)),
            debug_crash,
            heartbeat_alive: Arc::new(Mutex::new(true)),
        })
    }

    pub fn pid(&self) -> u32 {
        self.pid
    }

    pub fn is_alive(&mut self) -> bool {
        match self.child.try_wait() {
            Ok(Some(_)) => {
                *self.heartbeat_alive.lock().unwrap() = false;
                false
            }
            Ok(None) => {
                *self.heartbeat_alive.lock().unwrap() = true;
                true
            }
            Err(_) => {
                *self.heartbeat_alive.lock().unwrap() = false;
                false
            }
        }
    }

    pub fn check_heartbeat(&self) -> bool {
        *self.heartbeat_alive.lock().unwrap()
    }

    pub fn update_peak_memory(&self) {
        let mut sys = self.sys.lock().unwrap();
        sys.refresh_process(Pid::from_u32(self.pid));

        if let Some(process) = sys.process(Pid::from_u32(self.pid)) {
            let mem_mb = process.memory() as f64 / (1024.0 * 1024.0);
            let mut peak = self.peak_memory.lock().unwrap();
            if mem_mb > *peak {
                *peak = mem_mb;
            }
        }
    }

    pub fn get_peak_memory_mb(&self) -> f64 {
        *self.peak_memory.lock().unwrap()
    }

    pub fn elapsed(&self) -> Duration {
        self.started_at.elapsed()
    }

    pub fn get_log_tail(&self, lines: usize) -> String {
        let buf = self.log_buffer.lock().unwrap();
        let total = buf.len();
        let start = if total > lines { total - lines } else { 0 };
        buf[start..].join("\n")
    }

    pub fn get_full_log(&self) -> String {
        self.log_buffer.lock().unwrap().join("\n")
    }

    pub fn drain_pending_logs(&mut self) {
        if let Some(rx) = &self.stdout_rx {
            while let Ok(line) = rx.try_recv() {
                let mut buf = self.log_buffer.lock().unwrap();
                buf.push(format!("[stdout] {}", line));
            }
        }
        if let Some(rx) = &self.stderr_rx {
            while let Ok(line) = rx.try_recv() {
                let mut buf = self.log_buffer.lock().unwrap();
                buf.push(format!("[stderr] {}", line));
            }
        }
    }

    pub fn kill(&mut self) -> Result<(), String> {
        self.child
            .kill()
            .map_err(|e| format!("Failed to kill {}: {}", self.framework.name(), e))?;
        let _ = self.child.wait();
        Ok(())
    }

    pub fn wait_with_timeout(&mut self, timeout: Duration) -> Result<(), String> {
        let start = Instant::now();
        loop {
            if start.elapsed() >= timeout {
                return Err(format!(
                    "Timeout waiting for {} to exit",
                    self.framework.name()
                ));
            }
            match self.child.try_wait() {
                Ok(Some(_)) => return Ok(()),
                Ok(None) => thread::sleep(Duration::from_millis(100)),
                Err(e) => return Err(format!("Error waiting for {}: {}", self.framework.name(), e)),
            }
        }
    }

    pub fn framework(&self) -> Framework {
        self.framework
    }

    pub fn debug_crash(&self) -> bool {
        self.debug_crash
    }

    pub fn cleanup(mut self) -> CrashInfo {
        self.drain_pending_logs();

        let peak_memory = self.get_peak_memory_mb();
        let full_log = if self.debug_crash {
            Some(self.get_full_log())
        } else {
            None
        };

        let exit_status = self.child.wait().ok();
        let reason = match exit_status {
            Some(status) if !status.success() => {
                format!(
                    "Process exited with non-zero status: {}",
                    status.code().map(|c| c.to_string()).unwrap_or_else(|| "signal".to_string())
                )
            }
            Some(_) => "Process exited unexpectedly".to_string(),
            None => "Process killed or terminated".to_string(),
        };

        if let Some(handle) = self.stdout_handle.take() {
            let _ = handle.join();
        }
        if let Some(handle) = self.stderr_handle.take() {
            let _ = handle.join();
        }

        CrashInfo {
            reason,
            peak_memory_mb: peak_memory,
            crash_log: full_log,
            timestamp: chrono::Local::now(),
        }
    }
}

pub struct ProcessSupervisor {
    heartbeat_interval: Duration,
    timeout: Duration,
    debug_crash: bool,
}

impl ProcessSupervisor {
    pub fn new(timeout_secs: u64, debug_crash: bool) -> Self {
        ProcessSupervisor {
            heartbeat_interval: Duration::from_secs(5),
            timeout: Duration::from_secs(timeout_secs),
            debug_crash,
        }
    }

    pub fn heartbeat_interval(&self) -> Duration {
        self.heartbeat_interval
    }

    pub fn timeout(&self) -> Duration {
        self.timeout
    }

    pub fn debug_crash(&self) -> bool {
        self.debug_crash
    }

    pub fn check_timeout(&self, started_at: Instant) -> bool {
        started_at.elapsed() >= self.timeout
    }

    pub fn spawn_monitored(
        &self,
        framework: Framework,
        project_root: &PathBuf,
    ) -> Result<ManagedChild, String> {
        ManagedChild::spawn(framework, project_root, self.debug_crash)
    }
}

pub fn detect_crash_reason(
    child: &mut ManagedChild,
    last_heartbeat: Instant,
) -> RunStatus {
    if !child.is_alive() {
        let mem = child.get_peak_memory_mb();
        if mem > 2048.0 {
            return RunStatus::Crash;
        }
        return RunStatus::Crash;
    }

    if last_heartbeat.elapsed() > Duration::from_secs(30) {
        return RunStatus::Timeout;
    }

    RunStatus::Success
}

pub fn save_crash_log(crash_info: &CrashInfo, output_dir: &PathBuf) -> Result<PathBuf, String> {
    let timestamp = crash_info.timestamp.format("%Y%m%d_%H%M%S");
    let filename = format!("crash_log_{}.log", timestamp);
    let filepath = output_dir.join(&filename);

    if let Some(log) = &crash_info.crash_log {
        std::fs::write(&filepath, log)
            .map_err(|e| format!("Failed to write crash log: {}", e))?;
    }

    Ok(filepath)
}
