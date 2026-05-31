use crate::models::{AggregatedResult, CrashInfo, FrameworkResult, LatencyStats, ResourceUsage, RunStatus, ScenarioResult};
use std::collections::HashMap;

pub fn calculate_latency_stats(latencies: &[f64]) -> LatencyStats {
    if latencies.is_empty() {
        return LatencyStats {
            p50_ms: 0.0,
            p95_ms: 0.0,
            p99_ms: 0.0,
            min_ms: 0.0,
            max_ms: 0.0,
            avg_ms: 0.0,
        };
    }

    let mut sorted = latencies.to_vec();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap());

    let len = sorted.len();
    let min = sorted[0];
    let max = sorted[len - 1];
    let avg = sorted.iter().sum::<f64>() / len as f64;

    let p50 = percentile(&sorted, 50.0);
    let p95 = percentile(&sorted, 95.0);
    let p99 = percentile(&sorted, 99.0);

    LatencyStats {
        p50_ms: p50,
        p95_ms: p95,
        p99_ms: p99,
        min_ms: min,
        max_ms: max,
        avg_ms: avg,
    }
}

fn percentile(sorted: &[f64], p: f64) -> f64 {
    if sorted.is_empty() {
        return 0.0;
    }
    let n = sorted.len();
    let index = (p / 100.0) * (n - 1) as f64;
    let lower = index.floor() as usize;
    let upper = index.ceil() as usize;
    if lower == upper {
        sorted[lower]
    } else {
        let fraction = index - lower as f64;
        sorted[lower] * (1.0 - fraction) + sorted[upper] * fraction
    }
}

pub fn build_latency_distribution(latencies: &[f64], buckets: usize) -> Vec<(f64, u64)> {
    if latencies.is_empty() || buckets == 0 {
        return Vec::new();
    }

    let max_lat = latencies.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    let min_lat = latencies.iter().cloned().fold(f64::INFINITY, f64::min);
    
    if max_lat == min_lat {
        return vec![(max_lat, latencies.len() as u64)];
    }

    let bucket_size = (max_lat - min_lat) / buckets as f64;
    let mut dist = vec![0u64; buckets];

    for &lat in latencies {
        let mut bucket = (lat - min_lat) / bucket_size;
        if bucket >= buckets as f64 {
            bucket = (buckets - 1) as f64;
        }
        dist[bucket as usize] += 1;
    }

    dist.into_iter()
        .enumerate()
        .map(|(i, count)| (min_lat + bucket_size * (i as f64 + 0.5), count))
        .collect()
}

pub fn calculate_std_dev(values: &[f64]) -> f64 {
    if values.len() < 2 {
        return 0.0;
    }
    let mean = values.iter().sum::<f64>() / values.len() as f64;
    let variance = values
        .iter()
        .map(|v| (v - mean).powi(2))
        .sum::<f64>()
        / (values.len() - 1) as f64;
    variance.sqrt()
}

pub fn aggregate_results(results: &[FrameworkResult]) -> Vec<AggregatedResult> {
    let mut grouped: HashMap<(crate::models::Framework, crate::models::Scenario), Vec<&ScenarioResult>> =
        HashMap::new();
    let mut crash_count: HashMap<(crate::models::Framework, crate::models::Scenario), u32> = HashMap::new();

    for fr in results {
        if fr.status == RunStatus::Success {
            for (scenario, sr) in &fr.scenarios {
                grouped
                    .entry((fr.framework, *scenario))
                    .or_default()
                    .push(sr);
            }
        } else {
            for scenario in fr.scenarios.keys() {
                *crash_count
                    .entry((fr.framework, *scenario))
                    .or_insert(0) += 1;
            }
        }
    }

    grouped
        .into_iter()
        .map(|((framework, scenario), srs)| {
            let runs = srs.len() as u32;

            let qps_values: Vec<f64> = srs.iter().map(|s| s.qps).collect();
            let qps_avg = qps_values.iter().sum::<f64>() / qps_values.len() as f64;
            let qps_std = calculate_std_dev(&qps_values);

            let success_rate_avg = srs.iter().map(|s| s.success_rate).sum::<f64>() / srs.len() as f64;

            let p50_values: Vec<f64> = srs.iter().map(|s| s.latency.p50_ms).collect();
            let p95_values: Vec<f64> = srs.iter().map(|s| s.latency.p95_ms).collect();
            let p99_values: Vec<f64> = srs.iter().map(|s| s.latency.p99_ms).collect();
            let avg_values: Vec<f64> = srs.iter().map(|s| s.latency.avg_ms).collect();
            let min_values: Vec<f64> = srs.iter().map(|s| s.latency.min_ms).collect();
            let max_values: Vec<f64> = srs.iter().map(|s| s.latency.max_ms).collect();

            let latency_avg = LatencyStats {
                p50_ms: p50_values.iter().sum::<f64>() / p50_values.len() as f64,
                p95_ms: p95_values.iter().sum::<f64>() / p95_values.len() as f64,
                p99_ms: p99_values.iter().sum::<f64>() / p99_values.len() as f64,
                avg_ms: avg_values.iter().sum::<f64>() / avg_values.len() as f64,
                min_ms: min_values.iter().sum::<f64>() / min_values.len() as f64,
                max_ms: max_values.iter().sum::<f64>() / max_values.len() as f64,
            };

            let cpu_values: Vec<f32> = srs.iter().map(|s| s.avg_resource_usage.cpu_percent).collect();
            let mem_values: Vec<f64> = srs.iter().map(|s| s.avg_resource_usage.memory_mb).collect();

            let resource_avg = ResourceUsage {
                cpu_percent: cpu_values.iter().sum::<f32>() / cpu_values.len() as f32,
                memory_mb: mem_values.iter().sum::<f64>() / mem_values.len() as f64,
            };

            AggregatedResult {
                framework,
                scenario,
                qps_avg,
                qps_std,
                latency_avg,
                success_rate_avg,
                resource_avg,
                runs,
            }
        })
        .collect()
}

pub fn get_framework_crash_info(results: &[FrameworkResult]) -> Vec<(crate::models::Framework, RunStatus, Option<&CrashInfo>)> {
    let mut crash_entries: Vec<(crate::models::Framework, RunStatus, Option<&CrashInfo>)> = Vec::new();
    for fr in results {
        if fr.status != RunStatus::Success {
            crash_entries.push((fr.framework, fr.status, fr.crash_info.as_ref()));
        }
    }
    crash_entries
}

