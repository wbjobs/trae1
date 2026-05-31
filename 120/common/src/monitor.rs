use crate::models::ResourceUsage;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;
use sysinfo::{Pid, System};

pub struct ResourceMonitor {
    pid: u32,
    samples: Arc<Mutex<Vec<ResourceUsage>>>,
    stop_flag: Arc<Mutex<bool>>,
    handle: Option<thread::JoinHandle<()>>,
}

impl ResourceMonitor {
    pub fn new(pid: u32) -> Self {
        ResourceMonitor {
            pid,
            samples: Arc::new(Mutex::new(Vec::new())),
            stop_flag: Arc::new(Mutex::new(false)),
            handle: None,
        }
    }

    pub fn start(&mut self, interval_ms: u64) {
        let pid = self.pid;
        let samples = Arc::clone(&self.samples);
        let stop_flag = Arc::clone(&self.stop_flag);

        *stop_flag.lock().unwrap() = false;

        let handle = thread::spawn(move || {
            let mut sys = System::new_all();
            sys.refresh_all();

            while !*stop_flag.lock().unwrap() {
                sys.refresh_process(Pid::from_u32(pid));
                
                if let Some(process) = sys.process(Pid::from_u32(pid)) {
                    let usage = ResourceUsage {
                        cpu_percent: process.cpu_usage(),
                        memory_mb: process.memory() as f64 / (1024.0 * 1024.0),
                    };
                    samples.lock().unwrap().push(usage);
                }

                thread::sleep(Duration::from_millis(interval_ms));
            }
        });

        self.handle = Some(handle);
    }

    pub fn stop(&mut self) {
        *self.stop_flag.lock().unwrap() = true;
        if let Some(handle) = self.handle.take() {
            let _ = handle.join();
        }
    }

    pub fn get_average(&self) -> ResourceUsage {
        let samples = self.samples.lock().unwrap();
        if samples.is_empty() {
            return ResourceUsage {
                cpu_percent: 0.0,
                memory_mb: 0.0,
            };
        }

        let cpu_sum: f32 = samples.iter().map(|s| s.cpu_percent).sum();
        let mem_sum: f64 = samples.iter().map(|s| s.memory_mb).sum();
        let count = samples.len() as f64;

        ResourceUsage {
            cpu_percent: cpu_sum / count as f32,
            memory_mb: mem_sum / count,
        }
    }

    pub fn get_samples(&self) -> Vec<ResourceUsage> {
        self.samples.lock().unwrap().clone()
    }
}

impl Drop for ResourceMonitor {
    fn drop(&mut self) {
        self.stop();
    }
}
