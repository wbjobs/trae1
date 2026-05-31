use anyhow::{Context, Result};
use bytes::Bytes;
use http_body_util::BodyExt;
use hyper::body::Incoming;
use hyper_util::client::legacy::Client;
use hyper_util::rt::TokioExecutor;
use tokio::time::{timeout, Duration};

use crate::compression::Algo;

#[derive(Debug, Clone)]
pub struct FetchResult {
    pub status: http::StatusCode,
    pub body: Bytes,
    pub content_type: Option<String>,
    pub content_length: Option<u64>,
}

impl FetchResult {
    pub fn is_compressible(&self) -> bool {
        let ct = self
            .content_type
            .as_deref()
            .map(|s| s.to_ascii_lowercase())
            .unwrap_or_default();
        ct.starts_with("text/")
            || ct.contains("javascript")
            || ct.contains("json")
            || ct.contains("xml")
            || ct.contains("svg")
    }
}

#[derive(Clone)]
pub struct Origin {
    base: String,
    client: HttpClient,
}

type HttpClient = Client<hyper_util::client::legacy::connect::HttpConnector, Incoming>;

impl Origin {
    pub fn new(base: impl Into<String>) -> Self {
        let base = base.into().trim_end_matches('/').to_string();
        let client: HttpClient = Client::builder(TokioExecutor::new()).build_http();
        Self { base, client }
    }

    pub fn base(&self) -> &str {
        &self.base
    }

    pub async fn fetch(&self, path: &str) -> Result<FetchResult> {
        let normalized = if !path.starts_with('/') {
            format!("/{}", path)
        } else {
            path.to_string()
        };
        let url = format!("{}{}", self.base, normalized);
        let uri: hyper::Uri = url
            .parse()
            .with_context(|| format!("invalid origin url: {}", url))?;

        let req = hyper::Request::builder()
            .method(hyper::Method::GET)
            .uri(uri.clone())
            .header(
                http::header::ACCEPT_ENCODING,
                "identity",
            )
            .body(Incoming::empty())
            .context("build origin request")?;

        let fut = self.client.request(req);
        let res = timeout(Duration::from_secs(30), fut)
            .await
            .context("origin fetch timed out")?
            .with_context(|| format!("origin fetch failed: {}", uri))?;

        let status = res.status();
        let headers = res.headers().clone();
        let content_type = headers
            .get(http::header::CONTENT_TYPE)
            .and_then(|v| v.to_str().ok())
            .map(|s| s.to_string());
        let content_length = headers
            .get(http::header::CONTENT_LENGTH)
            .and_then(|v| v.to_str().ok())
            .and_then(|s| s.parse::<u64>().ok());

        let body = res.into_body();
        let collected = body.collect().await.context("read origin body")?;
        let bytes = collected.to_bytes();

        Ok(FetchResult {
            status,
            body: bytes,
            content_type,
            content_length,
        })
    }
}

pub fn prefers_compressed(accept_encoding: Option<&str>) -> bool {
    match accept_encoding {
        Some(v) => Algo::from_accept_encoding(v) != Algo::Identity,
        None => false,
    }
}
