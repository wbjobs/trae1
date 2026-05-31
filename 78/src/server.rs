use crate::cache::{apply_remaining_ttl, build_cache_key, message_key, Cache, CacheKey};
use crate::stats::Stats;
use crate::transport::UpstreamPool;
use async_trait::async_trait;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::Semaphore;
use tokio::time::{self, Duration};
use trust_dns_proto::op::{Header, Message, ResponseCode};
use trust_dns_proto::rr::{Record, RecordType};
use trust_dns_proto::serialize::binary::{BinDecodable, BinEncodable};
use trust_dns_server::authority::{MessageResponse, MessageResponseBuilder};
use trust_dns_server::server::{Request, RequestHandler, ResponseHandler, ResponseInfo};
use trust_dns_server::ServerFuture;

pub struct DnsHandler {
    pub cache: Arc<Cache>,
    pub upstream: Arc<UpstreamPool>,
    pub stats: Stats,
    pub prefetch_sem: Arc<Semaphore>,
}

impl DnsHandler {
    pub fn new(
        cache: Arc<Cache>,
        upstream: Arc<UpstreamPool>,
        stats: Stats,
        prefetch_parallel: usize,
    ) -> Self {
        Self {
            cache,
            upstream,
            stats,
            prefetch_sem: Arc::new(Semaphore::new(prefetch_parallel.max(1))),
        }
    }
}

fn error_info(code: ResponseCode) -> ResponseInfo {
    let mut header = Header::new();
    header.set_response_code(code);
    ResponseInfo::from(header)
}

fn build_msg_resp<'a, 'q>(
    request: &'q Request,
    header: Header,
    answers: &'a [Record],
    name_servers: &'a [Record],
    additionals: &'a [Record],
) -> MessageResponse<'q, 'a, std::slice::Iter<'a, Record>, std::slice::Iter<'a, Record>, std::iter::Empty<&'a Record>, std::slice::Iter<'a, Record>>
{
    let builder = MessageResponseBuilder::from_message_request(request);
    builder.build(header, answers.iter(), name_servers.iter(), std::iter::empty(), additionals.iter())
}

#[async_trait]
impl RequestHandler for DnsHandler {
    async fn handle_request<R: ResponseHandler>(
        &self,
        request: &Request,
        mut responder: R,
    ) -> ResponseInfo {
        self.stats.inc_queries();

        let query = request.query();
        let key = build_cache_key(&query.name().to_string(), query.query_type());

        if let Some(entry) = self.cache.get(&key) {
            let response = apply_remaining_ttl(&entry);
            if self.cache.should_prefetch(&entry) {
                self.spawn_prefetch(key.clone(), query.name().to_string(), query.query_type());
            }
            let header = response.header().clone();
            let answers: Vec<Record> = response.answers().iter().cloned().collect();
            let ns: Vec<Record> = response.name_servers().iter().cloned().collect();
            let adds: Vec<Record> = response.additionals().iter().cloned().collect();
            let msg_resp = build_msg_resp(request, header, &answers, &ns, &adds);
            return match responder.send_response(msg_resp).await {
                Ok(info) => info,
                Err(e) => {
                    tracing::error!(error = %e, "发送缓存响应失败");
                    error_info(ResponseCode::ServFail)
                }
            };
        }

        let msg_bytes = match request.to_bytes() {
            Ok(b) => b,
            Err(e) => {
                tracing::warn!(error = %e, "序列化请求失败");
                let mut header = request.header().clone();
                header.set_response_code(ResponseCode::FormErr);
                let msg_resp = MessageResponseBuilder::from_message_request(request)
                    .build_no_records(header);
                return match responder.send_response(msg_resp).await {
                    Ok(info) => info,
                    Err(_) => error_info(ResponseCode::ServFail),
                };
            }
        };

        let upstream_msg = match Message::from_bytes(&msg_bytes) {
            Ok(m) => m,
            Err(e) => {
                tracing::warn!(error = %e, "解析请求消息失败");
                let mut header = request.header().clone();
                header.set_response_code(ResponseCode::FormErr);
                let msg_resp = MessageResponseBuilder::from_message_request(request)
                    .build_no_records(header);
                return match responder.send_response(msg_resp).await {
                    Ok(info) => info,
                    Err(_) => error_info(ResponseCode::ServFail),
                };
            }
        };

        match self.upstream.query(&upstream_msg).await {
            Ok(response) => {
                if let Some(k) = message_key(&response) {
                    self.cache.insert(k, response.clone());
                }
                let header = response.header().clone();
                let answers: Vec<Record> = response.answers().iter().cloned().collect();
                let ns: Vec<Record> = response.name_servers().iter().cloned().collect();
                let adds: Vec<Record> = response.additionals().iter().cloned().collect();
                let msg_resp = build_msg_resp(request, header, &answers, &ns, &adds);
                match responder.send_response(msg_resp).await {
                    Ok(info) => info,
                    Err(e) => {
                        tracing::error!(error = %e, "发送响应失败");
                        error_info(ResponseCode::ServFail)
                    }
                }
            }
            Err(e) => {
                tracing::warn!(error = %e, "上游查询失败");
                let mut header = request.header().clone();
                header.set_response_code(ResponseCode::ServFail);
                let msg_resp = MessageResponseBuilder::from_message_request(request)
                    .build_no_records(header);
                match responder.send_response(msg_resp).await {
                    Ok(info) => info,
                    Err(_) => error_info(ResponseCode::ServFail),
                }
            }
        }
    }
}

#[derive(Clone)]
pub struct Handler(pub Arc<DnsHandler>);

#[async_trait]
impl RequestHandler for Handler {
    async fn handle_request<R: ResponseHandler>(
        &self,
        request: &Request,
        responder: R,
    ) -> ResponseInfo {
        self.0.handle_request(request, responder).await
    }
}

impl DnsHandler {
    fn spawn_prefetch(&self, key: CacheKey, name: String, rtype: RecordType) {
        let cache = self.cache.clone();
        let upstream = self.upstream.clone();
        let stats = self.stats.clone();
        let sem = self.prefetch_sem.clone();
        tokio::spawn(async move {
            let _permit = match sem.try_acquire() {
                Ok(p) => p,
                Err(_) => return,
            };
            let mut msg = Message::new();
            msg.set_id(rand::random::<u16>());
            msg.set_recursion_desired(true);
            let dns_name = match trust_dns_proto::rr::Name::from_utf8(&name) {
                Ok(n) => n,
                Err(_) => return,
            };
            let query = trust_dns_proto::op::Query::query(dns_name, rtype);
            msg.add_query(query);
            match upstream.query(&msg).await {
                Ok(response) => {
                    cache.insert(key, response);
                    stats.inc_prefetch();
                }
                Err(e) => {
                    tracing::debug!(error = %e, "prefetch 失败");
                }
            }
        });
    }
}

pub async fn run_cache_cleaner(cache: Arc<Cache>, interval: Duration) {
    let mut ticker = time::interval(interval);
    loop {
        ticker.tick().await;
        let removed = cache.cleanup_expired();
        if removed > 0 {
            tracing::debug!(removed, "清理过期缓存条目");
        }
    }
}

pub async fn start_dns_server(
    listen: SocketAddr,
    handler: Arc<DnsHandler>,
) -> anyhow::Result<()> {
    let server_handler = Handler(handler);
    let mut server = ServerFuture::new(server_handler);
    server.register_socket(tokio::net::UdpSocket::bind(listen).await?);
    tracing::info!(address = %listen, "DNS UDP 监听已启动");
    server.block_until_done().await?;
    Ok(())
}
