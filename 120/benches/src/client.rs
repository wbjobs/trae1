use common::models::{ResourceUsage, Scenario, ScenarioResult};
use common::stats::{build_latency_distribution, calculate_latency_stats};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::sync::Semaphore;
use tokio::time;

struct BenchmarkCounters {
    total: AtomicU64,
    success: AtomicU64,
    failed: AtomicU64,
}

impl BenchmarkCounters {
    fn new() -> Self {
        BenchmarkCounters {
            total: AtomicU64::new(0),
            success: AtomicU64::new(0),
            failed: AtomicU64::new(0),
        }
    }
}

pub struct BenchmarkClient {
    client: reqwest::Client,
    base_url: String,
}

impl BenchmarkClient {
    pub fn new(base_url: String) -> Self {
        let client = reqwest::Client::builder()
            .pool_idle_timeout(None)
            .http1_title_case_headers()
            .build()
            .expect("Failed to create HTTP client");

        BenchmarkClient { client, base_url }
    }

    pub async fn run_benchmark(
        &self,
        scenario: Scenario,
        concurrency: u32,
        duration: Duration,
    ) -> (ScenarioResult, Vec<f64>) {
        let counters = Arc::new(BenchmarkCounters::new());
        let latencies: Arc<std::sync::Mutex<Vec<f64>>> = Arc::new(std::sync::Mutex::new(Vec::new()));
        let semaphore = Arc::new(Semaphore::new(concurrency as usize));
        let stop_flag = Arc::new(tokio::sync::Notify::new());

        let start = Instant::now();

        let client_clone = self.client.clone();
        let base_url_clone = self.base_url.clone();
        let counters_clone = counters.clone();
        let latencies_clone = latencies.clone();
        let semaphore_clone = semaphore.clone();
        let stop_flag_clone = stop_flag.clone();

        let worker_handle = tokio::spawn(async move {
            let mut interval = time::interval(Duration::from_micros(1));
            loop {
                tokio::select! {
                    _ = stop_flag_clone.notified() => {
                        break;
                    }
                    _ = interval.tick() => {
                        let permit = semaphore_clone.clone().acquire_owned().await.unwrap();
                        let client = client_clone.clone();
                        let base_url = base_url_clone.clone();
                        let counters = counters_clone.clone();
                        let latencies = latencies_clone.clone();

                        tokio::spawn(async move {
                            let _permit = permit;
                            let req_start = Instant::now();

                            let success = match scenario {
                                Scenario::Json => test_json(&client, &base_url).await,
                                Scenario::DbQuery => test_db(&client, &base_url).await,
                                Scenario::Template => test_template(&client, &base_url).await,
                                Scenario::StaticFile => test_static(&client, &base_url).await,
                                Scenario::WebSocket => true,
                            };

                            let elapsed = req_start.elapsed().as_secs_f64() * 1000.0;
                            latencies.lock().unwrap().push(elapsed);
                            counters.total.fetch_add(1, Ordering::Relaxed);

                            if success {
                                counters.success.fetch_add(1, Ordering::Relaxed);
                            } else {
                                counters.failed.fetch_add(1, Ordering::Relaxed);
                            }
                        });
                    }
                }
            }
        });

        let stop_flag_clone = stop_flag.clone();
        tokio::spawn(async move {
            time::sleep(duration).await;
            stop_flag_clone.notify_waiters();
        });

        let _ = worker_handle.await;

        time::sleep(Duration::from_millis(100)).await;

        let elapsed = start.elapsed();
        let total = counters.total.load(Ordering::Relaxed);
        let success = counters.success.load(Ordering::Relaxed);
        let failed = counters.failed.load(Ordering::Relaxed);

        let latencies_vec = latencies.lock().unwrap().clone();
        let latency_stats = calculate_latency_stats(&latencies_vec);
        let latency_dist = build_latency_distribution(&latencies_vec, 20);

        let qps = total as f64 / elapsed.as_secs_f64();
        let success_rate = if total > 0 {
            success as f64 / total as f64
        } else {
            0.0
        };

        (
            ScenarioResult {
                scenario,
                qps,
                latency: latency_stats,
                success_rate,
                total_requests: total,
                success_requests: success,
                failed_requests: failed,
                avg_resource_usage: ResourceUsage {
                    cpu_percent: 0.0,
                    memory_mb: 0.0,
                },
                latency_distribution: latency_dist,
            },
            latencies_vec,
        )
    }

    pub async fn run_websocket_benchmark(
        &self,
        concurrency: u32,
        duration: Duration,
        port: u16,
    ) -> (ScenarioResult, Vec<f64>) {
        use futures_util::{SinkExt, StreamExt};
        use tokio_tungstenite::{connect_async, tungstenite::protocol::Message};

        let counters = Arc::new(BenchmarkCounters::new());
        let latencies: Arc<std::sync::Mutex<Vec<f64>>> = Arc::new(std::sync::Mutex::new(Vec::new()));
        let stop_flag = Arc::new(tokio::sync::Notify::new());
        let ws_url = format!("ws://127.0.0.1:{}/ws", port);

        let start = Instant::now();

        let mut handles = Vec::new();
        for _ in 0..concurrency {
            let ws_url = ws_url.clone();
            let counters = counters.clone();
            let latencies = latencies.clone();
            let stop_flag = stop_flag.clone();

            handles.push(tokio::spawn(async move {
                loop {
                    tokio::select! {
                        _ = stop_flag.notified() => {
                            break;
                        }
                        result = connect_async(&ws_url) => {
                            match result {
                                Ok((ws_stream, _)) => {
                                    let (mut write, mut read) = ws_stream.split();
                                    let mut interval = time::interval(Duration::from_micros(1));

                                    loop {
                                        tokio::select! {
                                            _ = stop_flag.notified() => {
                                                break;
                                            }
                                            _ = interval.tick() => {
                                                let req_start = Instant::now();
                                                let test_msg = "Hello, WebSocket!".to_string();

                                                if write.send(Message::Text(test_msg.clone())).await.is_ok() {
                                                    if let Some(Ok(msg)) = read.next().await {
                                                        if let Message::Text(text) = msg {
                                                            if text == test_msg {
                                                                let elapsed = req_start.elapsed().as_secs_f64() * 1000.0;
                                                                latencies.lock().unwrap().push(elapsed);
                                                                counters.total.fetch_add(1, Ordering::Relaxed);
                                                                counters.success.fetch_add(1, Ordering::Relaxed);
                                                                continue;
                                                            }
                                                        }
                                                    }
                                                    counters.total.fetch_add(1, Ordering::Relaxed);
                                                    counters.failed.fetch_add(1, Ordering::Relaxed);
                                                } else {
                                                    counters.total.fetch_add(1, Ordering::Relaxed);
                                                    counters.failed.fetch_add(1, Ordering::Relaxed);
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                    let _ = write.close().await;
                                }
                                Err(_) => {
                                    time::sleep(Duration::from_millis(10)).await;
                                }
                            }
                        }
                    }
                }
            }));
        }

        let stop_flag_clone = stop_flag.clone();
        tokio::spawn(async move {
            time::sleep(duration).await;
            stop_flag_clone.notify_waiters();
        });

        for handle in handles {
            let _ = handle.await;
        }

        time::sleep(Duration::from_millis(200)).await;

        let elapsed = start.elapsed();
        let total = counters.total.load(Ordering::Relaxed);
        let success = counters.success.load(Ordering::Relaxed);
        let failed = counters.failed.load(Ordering::Relaxed);

        let latencies_vec = latencies.lock().unwrap().clone();
        let latency_stats = calculate_latency_stats(&latencies_vec);
        let latency_dist = build_latency_distribution(&latencies_vec, 20);

        let qps = total as f64 / elapsed.as_secs_f64();
        let success_rate = if total > 0 {
            success as f64 / total as f64
        } else {
            0.0
        };

        (
            ScenarioResult {
                scenario: Scenario::WebSocket,
                qps,
                latency: latency_stats,
                success_rate,
                total_requests: total,
                success_requests: success,
                failed_requests: failed,
                avg_resource_usage: ResourceUsage {
                    cpu_percent: 0.0,
                    memory_mb: 0.0,
                },
                latency_distribution: latency_dist,
            },
            latencies_vec,
        )
    }
}

async fn test_json(client: &reqwest::Client, base_url: &str) -> bool {
    match client
        .get(&format!("{}/json", base_url))
        .send()
        .await
    {
        Ok(resp) => {
            if resp.status().is_success() {
                if let Ok(json) = resp.json::<serde_json::Value>().await {
                    json.get("status").and_then(|s| s.as_str()) == Some("ok")
                } else {
                    false
                }
            } else {
                false
            }
        }
        Err(_) => false,
    }
}

async fn test_db(client: &reqwest::Client, base_url: &str) -> bool {
    match client
        .get(&format!("{}/db", base_url))
        .send()
        .await
    {
        Ok(resp) => {
            if resp.status().is_success() {
                if let Ok(json) = resp.json::<serde_json::Value>().await {
                    json.is_array()
                } else {
                    false
                }
            } else {
                false
            }
        }
        Err(_) => false,
    }
}

async fn test_template(client: &reqwest::Client, base_url: &str) -> bool {
    match client
        .get(&format!("{}/template", base_url))
        .send()
        .await
    {
        Ok(resp) => resp.status().is_success(),
        Err(_) => false,
    }
}

async fn test_static(client: &reqwest::Client, base_url: &str) -> bool {
    match client
        .get(&format!("{}/static/test.txt", base_url))
        .send()
        .await
    {
        Ok(resp) => resp.status().is_success(),
        Err(_) => false,
    }
}
