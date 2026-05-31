mod cache;
mod compression;
mod config;
mod metrics;
mod origin;
mod singleflight;
mod strategy;

use std::net::SocketAddr;
use std::sync::Arc;
use std::time::Instant;

use anyhow::{Context, Result};
use bytes::Bytes;
use http::{HeaderName, HeaderValue, Method, StatusCode};
use http_body_util::{BodyExt, Full};
use hyper::body::Incoming;
use hyper::service::service_fn;
use hyper::{Request, Response};
use hyper_util::rt::{TokioExecutor, TokioIo};
use tokio::net::TcpListener;
use tokio::sync::Semaphore;
use tracing::{error, info, warn};

use crate::cache::DiskCache;
use crate::compression::{compress, Algo};
use crate::config::Config;
use crate::metrics::Metrics;
use crate::origin::Origin;
use crate::singleflight::Singleflight;
use crate::strategy::{LevelThresholds, Strategy};

type ResBody = Full<Bytes>;

#[derive(Clone)]
struct App {
    cache: DiskCache,
    origin: Origin,
    metrics: Metrics,
    config: Config,
    warmup_sem: Arc<Semaphore>,
    strategy: Strategy,
    singleflight: Singleflight,
}

#[tokio::main]
async fn main() -> Result<()> {
    let config = Config::from_args();
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| config.log_level.as_str().into()),
        )
        .init();

    let metrics = Metrics::new();
    let strategy = Strategy::new();
    let singleflight = Singleflight::new();
    let cache = DiskCache::new(&config, metrics.clone(), strategy.clone())?;
    let origin = Origin::new(&config.origin);
    let warmup_sem = Arc::new(Semaphore::new(config.warmup_concurrency));

    let app = App {
        cache,
        origin,
        metrics,
        config: config.clone(),
        warmup_sem,
        strategy,
        singleflight,
    };

    let addr: SocketAddr = config.bind.parse().context("invalid bind addr")?;
    let listener = TcpListener::bind(addr).await.context("bind listener")?;
    info!("cdn-edge listening on {}", addr);

    loop {
        let (stream, peer) = match listener.accept().await {
            Ok(v) => v,
            Err(e) => {
                warn!("accept error: {}", e);
                continue;
            }
        };
        let app = app.clone();
        tokio::spawn(async move {
            let io = TokioIo::new(stream);
            let svc = service_fn(move |req: Request<Incoming>| {
                let app = app.clone();
                async move {
                    let res = handle(app, req).await.unwrap_or_else(|e| {
                        error!("handler error: {:?}", e);
                        error_response(StatusCode::INTERNAL_SERVER_ERROR, "internal error")
                    });
                    Ok::<_, std::convert::Infallible>(res)
                }
            });
            if let Err(e) =
                hyper::server::conn::http1::Builder::new().serve_connection(io, svc).await
            {
                warn!("connection error from {}: {}", peer, e);
            }
        });
    }
}

async fn handle(app: App, req: Request<Incoming>) -> Result<Response<ResBody>> {
    let method = req.method().clone();
    let path = req.uri().path().to_string();
    let accept_encoding = req
        .headers()
        .get(http::header::ACCEPT_ENCODING)
        .and_then(|v| v.to_str().ok())
        .map(|s| s.to_string());

    match (method.clone(), path.as_str()) {
        (Method::GET, "/metrics") => {
            let body = app.metrics.encode();
            let res = Response::builder()
                .header(http::header::CONTENT_TYPE, "text/plain; version=0.0.4")
                .body(Full::new(Bytes::from(body)))
                .unwrap();
            app.metrics.inc_request("GET", 200);
            return Ok(res);
        }
        (Method::GET, "/health") => {
            return Ok(ok_text("ok"));
        }
        (Method::GET, "/api/strategy") => {
            return handle_get_strategy(app).await;
        }
        (Method::PUT, "/api/strategy") => {
            return handle_put_strategy(app, req).await;
        }
        (Method::POST, "/api/warmup") => {
            return handle_warmup(app, req).await;
        }
        (Method::GET, _) => {
            return serve_asset(app, path, accept_encoding.as_deref()).await;
        }
        _ => {
            app.metrics.inc_request(method.as_str(), 405);
            return Ok(error_response(StatusCode::METHOD_NOT_ALLOWED, "method not allowed"));
        }
    }
}

async fn serve_asset(
    app: App,
    url_path: String,
    accept_encoding: Option<&str>,
) -> Result<Response<ResBody>> {
    let requested = accept_encoding
        .map(Algo::from_accept_encoding)
        .unwrap_or(Algo::Identity);

    if let Some(bytes) = app.cache.get_bytes(&url_path, requested) {
        app.metrics.inc_cache_hit();
        app.metrics.inc_request("GET", 200);
        let mut res = Response::new(Full::new(bytes));
        apply_headers(&mut res, requested);
        return Ok(res);
    }

    app.metrics.inc_cache_miss();

    let fetched = match app.origin.fetch(&url_path).await {
        Ok(f) => f,
        Err(e) => {
            warn!("origin fetch failed: {:?}", e);
            app.metrics.inc_origin_fetch(0);
            app.metrics.inc_request("GET", 502);
            return Ok(error_response(
                StatusCode::BAD_GATEWAY,
                "origin unreachable",
            ));
        }
    };
    app.metrics.inc_origin_fetch(fetched.status.as_u16());

    if !fetched.status.is_success() {
        app.metrics.inc_request("GET", fetched.status.as_u16());
        let mut res = Response::new(Full::new(fetched.body));
        *res.status_mut() = fetched.status;
        return Ok(res);
    }

    if requested == Algo::Identity || !fetched.is_compressible() {
        app.metrics.inc_request("GET", 200);
        return Ok(ok_bytes(fetched.body, fetched.content_type));
    }

    let original_size = fetched.body.len() as u64;
    let uncompressed = fetched.body.clone();
    let content_type = fetched.content_type.clone();

    if app
        .singleflight
        .is_in_flight(&url_path, requested.content_encoding())
    {
        app.metrics.inc_request("GET", 200);
        return Ok(ok_bytes_pending(fetched.body, fetched.content_type));
    }

    let _notify = app
        .singleflight
        .try_acquire(&url_path, requested.content_encoding());

    {
        let app = app.clone();
        let url_path = url_path.clone();
        let uncompressed = uncompressed.clone();
        let content_type = content_type.clone();
        tokio::spawn(async move {
            let start = Instant::now();
            let lv = app.strategy.levels_for_size(original_size);
            match compress(
                &uncompressed,
                requested,
                lv.gzip,
                lv.brotli,
                lv.deflate,
            ) {
                Ok(compressed) => {
                    let dur = start.elapsed().as_secs_f64();
                    app.metrics
                        .observe_compression(requested, lv.tier.as_str(), dur);
                    app.metrics.record_compression(
                        requested,
                        lv.tier.as_str(),
                        original_size,
                        compressed.len() as u64,
                    );
                    if let Err(e) = app.cache.put(
                        &url_path,
                        requested,
                        Bytes::from(compressed),
                        content_type,
                    ) {
                        warn!("cache put failed: {:?}", e);
                    }
                }
                Err(e) => {
                    warn!("background compression failed: {:?}", e);
                }
            }
            app.singleflight
                .finish(&url_path, requested.content_encoding());
        });
    }

    app.metrics.inc_request("GET", 200);
    Ok(ok_bytes_pending(fetched.body, fetched.content_type))
}

async fn handle_get_strategy(app: App) -> Result<Response<ResBody>> {
    let thresholds = app.strategy.thresholds();
    let in_flight = app.singleflight.in_flight_count();
    let resp = serde_json::json!({
        "thresholds": thresholds,
        "tier_examples": {
            "small": {
                "max_bytes": thresholds.small_file_max_bytes,
                "gzip": thresholds.gzip_small,
                "brotli": thresholds.brotli_small,
                "deflate": thresholds.deflate_small,
            },
            "medium": {
                "min_bytes": thresholds.small_file_max_bytes + 1,
                "max_bytes": thresholds.medium_file_max_bytes,
                "gzip": thresholds.gzip_medium,
                "brotli": thresholds.brotli_medium,
                "deflate": thresholds.deflate_medium,
            },
            "large": {
                "min_bytes": thresholds.medium_file_max_bytes + 1,
                "gzip": thresholds.gzip_large,
                "brotli": thresholds.brotli_large,
                "deflate": thresholds.deflate_large,
            },
        },
        "in_flight_compressions": in_flight,
    });
    Ok(json_response(StatusCode::OK, &resp))
}

async fn handle_put_strategy(app: App, req: Request<Incoming>) -> Result<Response<ResBody>> {
    let body = req.into_body().collect().await.context("read body")?;
    let bytes = body.to_bytes();
    let new_thresholds: LevelThresholds =
        serde_json::from_slice(&bytes).context("parse strategy payload")?;

    let validated = validate_thresholds(&new_thresholds)?;
    app.strategy.update(validated.clone());

    info!("compression strategy updated via API");

    let resp = serde_json::json!({
        "status": "updated",
        "thresholds": validated,
    });
    Ok(json_response(StatusCode::OK, &resp))
}

fn validate_thresholds(t: &LevelThresholds) -> Result<LevelThresholds> {
    use anyhow::bail;

    if t.small_file_max_bytes == 0 {
        bail!("small_file_max_bytes must be > 0");
    }
    if t.medium_file_max_bytes <= t.small_file_max_bytes {
        bail!("medium_file_max_bytes must be > small_file_max_bytes");
    }
    for (name, val) in &[
        ("gzip_small", t.gzip_small),
        ("gzip_medium", t.gzip_medium),
        ("gzip_large", t.gzip_large),
        ("deflate_small", t.deflate_small),
        ("deflate_medium", t.deflate_medium),
        ("deflate_large", t.deflate_large),
    ] {
        if *val < 1 || *val > 9 {
            bail!("{} must be in range [1, 9]", name);
        }
    }
    for (name, val) in &[
        ("brotli_small", t.brotli_small),
        ("brotli_medium", t.brotli_medium),
        ("brotli_large", t.brotli_large),
    ] {
        if *val < 1 || *val > 11 {
            bail!("{} must be in range [1, 11]", name);
        }
    }
    Ok(t.clone())
}

async fn handle_warmup(app: App, req: Request<Incoming>) -> Result<Response<ResBody>> {
    let body = req.into_body().collect().await.context("read body")?;
    let bytes = body.to_bytes();
    let payload: WarmupRequest =
        serde_json::from_slice(&bytes).context("parse warmup payload")?;

    for path in payload.paths.iter() {
        let permit = app
            .warmup_sem
            .clone()
            .acquire_owned()
            .await
            .context("acquire warmup permit")?;
        let app = app.clone();
        let path = path.clone();
        tokio::spawn(async move {
            let _permit = permit;
            match warmup_one(&app, &path).await {
                Ok(()) => {
                    app.metrics.inc_warmup("ok");
                    info!("warmed up {}", path);
                }
                Err(e) => {
                    app.metrics.inc_warmup("err");
                    warn!("warmup {} failed: {:?}", path, e);
                }
            }
        });
    }

    let resp = serde_json::json!({"accepted": payload.paths.len()});
    Ok(json_response(StatusCode::ACCEPTED, &resp))
}

async fn warmup_one(app: &App, path: &str) -> Result<()> {
    let mut missing = Vec::new();
    for algo in Algo::all() {
        if !app.cache.contains(path, algo) {
            missing.push(algo);
        }
    }
    if missing.is_empty() {
        return Ok(());
    }

    let fetched = app.origin.fetch(path).await?;
    if !fetched.status.is_success() || !fetched.is_compressible() {
        return Ok(());
    }
    let body = fetched.body.clone();
    let ct = fetched.content_type.clone();

    for algo in missing {
        let app = app.clone();
        let body = body.clone();
        let ct = ct.clone();
        let path = path.to_string();
        tokio::spawn(async move {
            let _ = app
                .cache
                .put_compressed_async(path, algo, body, ct)
                .await;
        });
    }
    Ok(())
}

fn apply_headers(res: &mut Response<ResBody>, algo: Algo) {
    let headers = res.headers_mut();
    let name = HeaderName::from_static("content-encoding");
    let value = HeaderValue::from_static(match algo {
        Algo::Gzip => "gzip",
        Algo::Brotli => "br",
        Algo::Deflate => "deflate",
        Algo::Identity => "identity",
    });
    headers.insert(name, value);
    headers.insert(
        http::header::CACHE_CONTROL,
        HeaderValue::from_static("public, max-age=604800"),
    );
    headers.insert(
        HeaderName::from_static("x-cache"),
        HeaderValue::from_static("HIT"),
    );
}

fn ok_text(s: &str) -> Response<ResBody> {
    Response::new(Full::new(Bytes::from_static(s.as_bytes())))
}

fn ok_bytes(bytes: Bytes, content_type: Option<String>) -> Response<ResBody> {
    let mut res = Response::new(Full::new(bytes));
    if let Some(ct) = content_type {
        if let Ok(v) = HeaderValue::from_str(&ct) {
            res.headers_mut().insert(http::header::CONTENT_TYPE, v);
        }
    }
    res.headers_mut().insert(
        HeaderName::from_static("x-cache"),
        HeaderValue::from_static("MISS"),
    );
    res
}

fn ok_bytes_pending(bytes: Bytes, content_type: Option<String>) -> Response<ResBody> {
    let mut res = Response::new(Full::new(bytes));
    if let Some(ct) = content_type {
        if let Ok(v) = HeaderValue::from_str(&ct) {
            res.headers_mut().insert(http::header::CONTENT_TYPE, v);
        }
    }
    res.headers_mut().insert(
        HeaderName::from_static("x-cache"),
        HeaderValue::from_static("MISS, Compression-Pending"),
    );
    res
}

fn error_response(status: StatusCode, msg: &str) -> Response<ResBody> {
    let mut res = Response::new(Full::new(Bytes::from_static(msg.as_bytes())));
    *res.status_mut() = status;
    res
}

fn json_response(status: StatusCode, value: &serde_json::Value) -> Response<ResBody> {
    let body = serde_json::to_vec(value).unwrap_or_default();
    let mut res = Response::new(Full::new(Bytes::from(body)));
    *res.status_mut() = status;
    res.headers_mut().insert(
        http::header::CONTENT_TYPE,
        HeaderValue::from_static("application/json"),
    );
    res
}

#[derive(serde::Deserialize)]
struct WarmupRequest {
    paths: Vec<String>,
}
