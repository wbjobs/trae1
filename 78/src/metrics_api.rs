use crate::stats::Stats;
use axum::{response::Json, routing::get, Router};
use std::net::SocketAddr;

pub async fn serve_metrics(listen: SocketAddr, stats: Stats) -> anyhow::Result<()> {
    let app = Router::new()
        .route("/", get(|| async { "DNSCrypt resolver stats API" }))
        .route("/healthz", get(|| async { "ok" }))
        .route(
            "/stats",
            get({
                let s = stats.clone();
                move || {
                    let s = s.clone();
                    async move { Json(s.snapshot()) }
                }
            }),
        )
        .route(
            "/cache",
            get({
                let s = stats.clone();
                move || {
                    let s = s.clone();
                    async move {
                        let snap = s.snapshot();
                        Json(serde_json::json!({
                            "hits": snap.cache.hits,
                            "misses": snap.cache.misses,
                            "hit_rate": if snap.cache.hits + snap.cache.misses > 0 {
                                snap.cache.hits as f64 / (snap.cache.hits + snap.cache.misses) as f64
                            } else {
                                0.0
                            },
                            "inserts": snap.cache.inserts,
                            "evictions": snap.cache.evictions,
                            "entries": snap.cache.entries,
                        }))
                    }
                }
            }),
        )
        .route(
            "/upstreams",
            get({
                let s = stats.clone();
                move || {
                    let s = s.clone();
                    async move {
                        let snap = s.snapshot();
                        Json(serde_json::json!({
                            "active": snap.active_upstream,
                            "total_queries": snap.total_queries,
                            "prefetch_count": snap.prefetch_count,
                            "upstreams": snap.upstreams,
                        }))
                    }
                }
            }),
        );

    tracing::info!(address = %listen, "metrics REST API 监听启动");
    let listener = tokio::net::TcpListener::bind(listen).await?;
    axum::serve(listener, app).await?;
    Ok(())
}
