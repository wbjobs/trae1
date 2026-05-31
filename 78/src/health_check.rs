use crate::transport::UpstreamPool;
use std::sync::Arc;
use std::time::Duration;
use tokio::time::interval;

pub async fn run_health_checks(
    pool: Arc<UpstreamPool>,
    interval_secs: u64,
) {
    let mut ticker = interval(Duration::from_secs(interval_secs.max(5)));
    loop {
        ticker.tick().await;
        let mut healthy_results: Vec<(usize, bool, Option<f64>)> = Vec::new();

        for (idx, transport) in pool.transports.iter().enumerate() {
            let probe = transport.health_probe().await;
            let healthy = probe.is_some();
            transport.set_healthy(healthy).await;
            healthy_results.push((idx, healthy, probe));
            tracing::debug!(
                upstream = %transport.name(),
                protocol = %transport.protocol(),
                healthy,
                latency_ms = probe.unwrap_or(0.0),
                "健康检测完成"
            );
        }

        let healthy_count = healthy_results.iter().filter(|(_, h, _)| *h).count();
        tracing::info!(
            total = pool.transports.len(),
            healthy = healthy_count,
            "健康检测汇总: {}/{} 上游可用",
            healthy_count,
            pool.transports.len()
        );
    }
}