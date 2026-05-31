use crate::compression::{Compressor, CompressionStats};
use crate::config::{Config, InstanceConfig};
use crate::protocol::*;
use crate::stats::{classify_query, QueryType, SharedStats};
use crate::tls::{TlsContext, start_hot_reload_task};
use bytes::{Bytes, BytesMut};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio_rustls::{TlsAcceptor, client::TlsStream as ClientTlsStream, server::TlsStream as ServerTlsStream};
use tracing::{debug, error, info, warn};

enum Connection {
    Plain(TcpStream),
    TlsClient(ServerTlsStream<TcpStream>),
    TlsServer(ClientTlsStream<TcpStream>),
}

impl Connection {
    async fn read_buf(&mut self, buf: &mut BytesMut) -> std::io::Result<usize> {
        match self {
            Connection::Plain(s) => s.read_buf(buf).await,
            Connection::TlsClient(s) => s.read_buf(buf).await,
            Connection::TlsServer(s) => s.read_buf(buf).await,
        }
    }

    async fn write_all(&mut self, buf: &[u8]) -> std::io::Result<()> {
        match self {
            Connection::Plain(s) => s.write_all(buf).await,
            Connection::TlsClient(s) => s.write_all(buf).await,
            Connection::TlsServer(s) => s.write_all(buf).await,
        }
    }
}

pub struct ProxyServer {
    config: Arc<Config>,
    stats: SharedStats,
}

impl ProxyServer {
    pub fn new(config: Arc<Config>, stats: SharedStats) -> Self {
        Self { config, stats }
    }

    pub async fn start(&self) -> Result<(), String> {
        let mut handles = Vec::new();

        for instance in &self.config.instance {
            let stats = self.stats.clone();
            stats.register_instance(&instance.name);

            let config = self.config.clone();
            let instance_config = instance.clone();

            let handle = tokio::spawn(async move {
                if let Err(e) = run_instance(config, instance_config, stats).await {
                    error!("Instance '{}' error: {}", instance_config.name, e);
                }
            });

            handles.push(handle);
            info!(
                "Starting proxy instance '{}' on {} -> {}",
                instance.name,
                instance.listen_addr,
                instance
                    .target_addr
                    .as_deref()
                    .or(instance.target_socket.as_deref())
                    .unwrap_or("unknown")
            );
        }

        for handle in handles {
            if let Err(e) = handle.await {
                error!("Instance task error: {}", e);
            }
        }

        Ok(())
    }
}

async fn run_instance(
    config: Arc<Config>,
    instance: InstanceConfig,
    stats: SharedStats,
) -> Result<(), String> {
    let listener = TcpListener::bind(&instance.listen_addr)
        .await
        .map_err(|e| format!("Failed to bind to {}: {}", instance.listen_addr, e))?;

    let tls_config = config.get_tls_config(&instance);
    let tls_context = TlsContext::new(tls_config.clone(), &instance.name);
    
    if tls_config.client.enabled || tls_config.server.enabled {
        tls_context.reload()?;
        
        if tls_config.hot_reload && tls_config.reload_interval_secs > 0 {
            let tls_context_clone = tls_context.clone();
            let instance_name = instance.name.clone();
            let interval = tls_config.reload_interval_secs;
            
            tokio::spawn(async move {
                start_hot_reload_task(tls_context_clone, instance_name, interval).await;
            });
        }
    }

    info!(
        "Instance '{}' listening on {} (TLS: client={}, server={})",
        instance.name, 
        instance.listen_addr,
        tls_config.client.enabled,
        tls_config.server.enabled
    );

    loop {
        let (client_stream, client_addr) = listener
            .accept()
            .await
            .map_err(|e| format!("Failed to accept connection: {}", e))?;

        debug!("New connection from {} for instance '{}'", client_addr, instance.name);

        stats.record_connection(&instance.name);

        let config = config.clone();
        let instance = instance.clone();
        let stats = stats.clone();
        let tls_context = tls_context.clone();

        tokio::spawn(async move {
            if let Err(e) =
                handle_client(config, instance, stats, tls_context, client_stream, client_addr).await
            {
                warn!(
                    "Connection from {} for instance '{}' ended with error: {}",
                    client_addr, instance.name, e
                );
            }
        });
    }
}

async fn handle_client(
    config: Arc<Config>,
    instance: InstanceConfig,
    stats: SharedStats,
    tls_context: TlsContext,
    mut client_stream: TcpStream,
    client_addr: std::net::SocketAddr,
) -> Result<(), String> {
    let target_addr = instance
        .target_addr
        .clone()
        .ok_or_else(|| "target_addr is required".to_string())?;

    let server_stream = TcpStream::connect(&target_addr)
        .await
        .map_err(|e| format!("Failed to connect to target {}: {}", target_addr, e))?;

    let mut client_conn = Connection::Plain(client_stream);
    let mut server_conn = Connection::Plain(server_stream);

    let mut client_buffer = BytesMut::with_capacity(8192);
    let mut server_buffer = BytesMut::with_capacity(8192);

    let mut started = false;
    let mut client_tls_negotiated = false;
    let mut server_tls_negotiated = false;

    let client_tls_enabled = tls_context.client_tls_enabled();
    let server_tls_enabled = tls_context.server_tls_enabled();

    let compression_enabled = config.is_compression_enabled(&instance);
    let min_compress_size = config.get_min_compress_size(&instance);
    let compression_level = config.get_compression_level(&instance);
    let adaptive_mode = config.is_adaptive_mode_enabled(&instance);
    let mut compressor = Compressor::new(compression_level, min_compress_size);
    compressor.set_adaptive_mode(adaptive_mode);

    let mut prepared_statements: HashMap<String, String> = HashMap::new();
    let mut current_row_description: Option<RowDescriptionMessage> = None;

    loop {
        tokio::select! {
            result = client_conn.read_buf(&mut client_buffer) => {
                match result {
                    Ok(0) => {
                        debug!("Client {} disconnected", client_addr);
                        break;
                    }
                    Ok(n) => {
                        stats.record_bytes_from_client(&instance.name, n as u64);

                        if !started {
                            if !client_tls_negotiated && client_tls_enabled {
                                if is_ssl_request(&client_buffer) {
                                    debug!("Client {} requested SSL", client_addr);
                                    
                                    let server_config = tls_context.get_server_config()
                                        .ok_or_else(|| "TLS配置未就绪".to_string())?;
                                    let acceptor = TlsAcceptor::from(server_config);
                                    
                                    let tcp_stream = match client_conn {
                                        Connection::Plain(s) => s,
                                        _ => return Err("无效的连接状态".to_string()),
                                    };
                                    
                                    let tls_stream = acceptor.accept(tcp_stream)
                                        .await
                                        .map_err(|e| format!("TLS握手失败: {}", e))?;
                                    
                                    client_conn = Connection::TlsClient(tls_stream);
                                    client_tls_negotiated = true;
                                    client_buffer.clear();
                                    debug!("Client {} TLS握手成功", client_addr);
                                    continue;
                                } else {
                                    if client_tls_enabled {
                                        return Err("客户端必须使用TLS连接".to_string());
                                    }
                                }
                            }
                            
                            if !server_tls_negotiated && server_tls_enabled {
                                let ssl_request = Bytes::from_static(&[0, 0, 0, 8, 4, -46, 22, 47]);
                                server_conn.write_all(&ssl_request).await
                                    .map_err(|e| e.to_string())?;
                                
                                let mut ssl_response = [0u8; 1];
                                server_conn.read_buf(&mut BytesMut::from(&mut ssl_response[..])).await
                                    .map_err(|e| e.to_string())?;
                                
                                if ssl_response[0] == b'S' {
                                    let client_config = tls_context.get_client_config()
                                        .ok_or_else(|| "TLS客户端配置未就绪".to_string())?;
                                    let connector = tokio_rustls::TlsConnector::from(client_config);
                                    
                                    let tcp_stream = match server_conn {
                                        Connection::Plain(s) => s,
                                        _ => return Err("无效的服务器连接状态".to_string()),
                                    };
                                    
                                    let server_name = target_addr.split(':').next()
                                        .unwrap_or("localhost")
                                        .to_string();
                                    let dns_name = rustls::ServerName::try_from(server_name.as_str())
                                        .map_err(|e| format!("无效的服务器名称: {}", e))?;
                                    
                                    let tls_stream = connector.connect(dns_name, tcp_stream)
                                        .await
                                        .map_err(|e| format!("服务器TLS握手失败: {}", e))?;
                                    
                                    server_conn = Connection::TlsServer(tls_stream);
                                    server_tls_negotiated = true;
                                    debug!("Server TLS握手成功");
                                } else {
                                    return Err("服务器不支持TLS连接".to_string());
                                }
                            }

                            if let Some((protocol_version, _)) = try_parse_startup(&mut client_buffer)
                                .map_err(|e| e.to_string())?
                            {
                                started = true;
                                debug!("Startup message from {}, protocol version: {}", client_addr, protocol_version);

                                let startup_bytes = Bytes::copy_from_slice(&client_buffer[..]);
                                server_conn.write_all(&startup_bytes).await.map_err(|e| e.to_string())?;
                                client_buffer.clear();
                                continue;
                            }
                        }

                        while let Some(msg) = try_parse_message(&mut client_buffer).map_err(|e| e.to_string())? {
                            handle_client_message(
                                &msg,
                                &instance,
                                &stats,
                                compression_enabled,
                                &compressor,
                                &mut prepared_statements,
                                &mut server_conn,
                            ).await?;
                        }
                    }
                    Err(e) => {
                        return Err(format!("Error reading from client: {}", e));
                    }
                }
            }
            result = server_conn.read_buf(&mut server_buffer) => {
                match result {
                    Ok(0) => {
                        debug!("Server closed connection for {}", client_addr);
                        break;
                    }
                    Ok(n) => {
                        stats.record_bytes_from_server(&instance.name, n as u64);

                        while let Some(msg) = try_parse_message(&mut server_buffer).map_err(|e| e.to_string())? {
                            let response = handle_server_message(
                                &msg,
                                &instance,
                                &stats,
                                compression_enabled,
                                &mut current_row_description,
                            )?;

                            let encoded = response.encode();
                            stats.record_bytes_to_client(&instance.name, encoded.len() as u64);
                            client_conn.write_all(&encoded).await.map_err(|e| e.to_string())?;
                        }
                    }
                    Err(e) => {
                        return Err(format!("Error reading from server: {}", e));
                    }
                }
            }
        }
    }

    stats.record_disconnection(&instance.name);
    Ok(())
}

async fn handle_client_message(
    msg: &PgMessage,
    instance: &InstanceConfig,
    stats: &SharedStats,
    compression_enabled: bool,
    compressor: &Compressor,
    prepared_statements: &mut HashMap<String, String>,
    server_conn: &mut Connection,
) -> Result<(), String> {
    match msg.msg_type {
        MSG_TYPE_QUERY => {
            if let Ok(query_msg) = QueryMessage::from_payload(&msg.payload) {
                let query_type = classify_query(&query_msg.query);
                stats.record_query(&instance.name, query_type);
                debug!("Query ({}): {}", query_type_to_str(query_type), query_msg.query);

                if compression_enabled {
                    if let Some(modified) =
                        process_query_for_compression(&query_msg.query, compressor, stats, instance)
                    {
                        let new_msg = QueryMessage::new(&modified);
                        let encoded = new_msg.to_message().encode();
                        stats.record_bytes_to_server(&instance.name, encoded.len() as u64);
                        server_conn
                            .write_all(&encoded)
                            .await
                            .map_err(|e| e.to_string())?;
                        return Ok(());
                    }
                }
            }

            let encoded = msg.encode();
            stats.record_bytes_to_server(&instance.name, encoded.len() as u64);
            server_conn
                .write_all(&encoded)
                .await
                .map_err(|e| e.to_string())?;
        }

        MSG_TYPE_PARSE => {
            if let Ok(parse_msg) = ParseMessage::from_payload(&msg.payload) {
                prepared_statements.insert(
                    parse_msg.statement_name.clone(),
                    parse_msg.query.clone(),
                );
                debug!(
                    "Parse statement '{}': {}",
                    parse_msg.statement_name, parse_msg.query
                );
            }

            let encoded = msg.encode();
            stats.record_bytes_to_server(&instance.name, encoded.len() as u64);
            server_conn
                .write_all(&encoded)
                .await
                .map_err(|e| e.to_string())?;
        }

        MSG_TYPE_BIND => {
            if compression_enabled {
                if let Ok(bind_msg) = BindMessage::from_payload(&msg.payload) {
                    let query = prepared_statements.get(&bind_msg.statement_name).cloned();

                    if let Some(query) = query {
                        let query_type = classify_query(&query);
                        if query_type == QueryType::Insert || query_type == QueryType::Update {
                            let modified = compress_bind_params(
                                bind_msg,
                                compressor,
                                stats,
                                instance,
                            );
                            let new_payload = modified.encode();
                            let new_msg = PgMessage::new(MSG_TYPE_BIND, new_payload);
                            let encoded = new_msg.encode();
                            stats.record_bytes_to_server(&instance.name, encoded.len() as u64);
                            server_conn
                                .write_all(&encoded)
                                .await
                                .map_err(|e| e.to_string())?;
                            return Ok(());
                        }
                    }
                }
            }

            let encoded = msg.encode();
            stats.record_bytes_to_server(&instance.name, encoded.len() as u64);
            server_conn
                .write_all(&encoded)
                .await
                .map_err(|e| e.to_string())?;
        }

        MSG_TYPE_TERMINATE => {
            let encoded = msg.encode();
            stats.record_bytes_to_server(&instance.name, encoded.len() as u64);
            server_conn
                .write_all(&encoded)
                .await
                .map_err(|e| e.to_string())?;
            debug!("Client sent terminate");
        }

        _ => {
            let encoded = msg.encode();
            stats.record_bytes_to_server(&instance.name, encoded.len() as u64);
            server_conn
                .write_all(&encoded)
                .await
                .map_err(|e| e.to_string())?;
        }
    }

    Ok(())
}

fn handle_server_message(
    msg: &PgMessage,
    instance: &InstanceConfig,
    stats: &SharedStats,
    compression_enabled: bool,
    current_row_description: &mut Option<RowDescriptionMessage>,
) -> Result<PgMessage, String> {
    match msg.msg_type {
        MSG_TYPE_ROW_DESCRIPTION => {
            if let Ok(desc) = RowDescriptionMessage::from_payload(&msg.payload) {
                *current_row_description = Some(desc);
            }
            Ok(msg.clone())
        }

        MSG_TYPE_DATA_ROW => {
            if compression_enabled {
                if let Ok(row) = DataRowMessage::from_payload(&msg.payload) {
                    let modified = decompress_data_row(row, stats, instance);
                    let new_payload = modified.encode();
                    return Ok(PgMessage::new(MSG_TYPE_DATA_ROW, new_payload));
                }
            }
            Ok(msg.clone())
        }

        _ => Ok(msg.clone()),
    }
}

fn process_query_for_compression(
    query: &str,
    compressor: &Compressor,
    stats: &SharedStats,
    instance: &InstanceConfig,
) -> Option<String> {
    let query_type = classify_query(query);

    match query_type {
        QueryType::Insert => compress_insert_query(query, compressor, stats, instance),
        QueryType::Update => compress_update_query(query, compressor, stats, instance),
        _ => None,
    }
}

fn compress_insert_query(
    query: &str,
    compressor: &Compressor,
    stats: &SharedStats,
    instance: &InstanceConfig,
) -> Option<String> {
    let upper_query = query.to_uppercase();

    let values_pos = upper_query.find("VALUES")?;
    let before_values = &query[..values_pos];
    let values_part = &query[values_pos + 6..];

    let mut result = String::from(before_values);
    result.push_str("VALUES ");

    let mut depth = 0;
    let mut current_start = 0;
    let mut in_string = false;
    let mut string_char = ' ';
    let mut in_value = false;
    let mut value_start = 0;
    let mut processed = String::new();

    for (i, c) in values_part.char_indices() {
        match c {
            '\'' if !in_string => {
                in_string = true;
                string_char = '\'';
                if !in_value {
                    in_value = true;
                    value_start = i;
                }
            }
            '\'' if in_string && string_char == '\'' => {
                if i + 1 < values_part.len() && values_part.as_bytes()[i + 1] == b'\'' {
                    continue;
                }
                in_string = false;
            }
            '"' if !in_string => {
                in_string = true;
                string_char = '"';
                if !in_value {
                    in_value = true;
                    value_start = i;
                }
            }
            '"' if in_string && string_char == '"' => {
                in_string = false;
            }
            '(' if !in_string => {
                if depth == 0 && !in_value {
                    processed.push_str(&values_part[current_start..i]);
                    current_start = i + 1;
                }
                depth += 1;
            }
            ')' if !in_string => {
                depth -= 1;
                if depth == 0 {
                    let value_content = &values_part[current_start..i];
                    let compressed = try_compress_value(value_content, compressor, stats, instance);
                    processed.push('(');
                    processed.push_str(&compressed);
                    processed.push(')');
                    current_start = i + 1;
                }
            }
            _ => {}
        }
    }

    if current_start < values_part.len() {
        processed.push_str(&values_part[current_start..]);
    }

    result.push_str(&processed);
    Some(result)
}

fn try_compress_value(
    value: &str,
    compressor: &Compressor,
    stats: &SharedStats,
    instance: &InstanceConfig,
) -> String {
    let trimmed = value.trim();
    if trimmed.starts_with('\'') && trimmed.ends_with('\'') && trimmed.len() > 2 {
        let content = &trimmed[1..trimmed.len() - 1];
        let content_bytes = content.as_bytes();

        if compressor.should_compress(content_bytes) {
            match compressor.compress(content_bytes) {
                Ok((compressed, decision)) => {
                    match decision {
                        crate::compression::CompressDecision::Compress => {
                            stats.record_compression(
                                instance.name.as_str(),
                                content_bytes.len() as u64,
                                compressed.len() as u64,
                            );
                            let encoded = encode_compressed(&compressed);
                            return format!("'{}'", encoded);
                        }
                        _ => {
                            stats.record_skipped(
                                instance.name.as_str(),
                                content_bytes.len() as u64,
                                decision,
                            );
                        }
                    }
                }
                Err(e) => {
                    warn!("Compression failed: {}", e);
                }
            }
        }
    }
    value.to_string()
}

fn compress_update_query(
    query: &str,
    _compressor: &Compressor,
    _stats: &SharedStats,
    _instance: &InstanceConfig,
) -> Option<String> {
    None
}

fn compress_bind_params(
    bind_msg: BindMessage,
    compressor: &Compressor,
    stats: &SharedStats,
    instance: &InstanceConfig,
) -> BindMessage {
    let mut new_values = Vec::with_capacity(bind_msg.param_values.len());

    for value in &bind_msg.param_values {
        match value {
            Some(v) => {
                if compressor.should_compress(v) {
                    match compressor.compress(v) {
                        Ok((compressed, decision)) => {
                            match decision {
                                crate::compression::CompressDecision::Compress => {
                                    stats.record_compression(
                                        &instance.name,
                                        v.len() as u64,
                                        compressed.len() as u64,
                                    );
                                    new_values.push(Some(Bytes::from(compressed)));
                                }
                                _ => {
                                    stats.record_skipped(&instance.name, v.len() as u64, decision);
                                    new_values.push(Some(v.clone()));
                                }
                            }
                        }
                        Err(_) => {
                            new_values.push(Some(v.clone()));
                        }
                    }
                } else {
                    new_values.push(Some(v.clone()));
                }
            }
            None => new_values.push(None),
        }
    }

    BindMessage {
        portal_name: bind_msg.portal_name,
        statement_name: bind_msg.statement_name,
        param_format_codes: bind_msg.param_format_codes,
        param_values: new_values,
        result_format_codes: bind_msg.result_format_codes,
    }
}

fn decompress_data_row(
    row: DataRowMessage,
    stats: &SharedStats,
    instance: &InstanceConfig,
) -> DataRowMessage {
    let mut new_values = Vec::with_capacity(row.values.len());

    for value in &row.values {
        match value {
            Some(v) => {
                if Compressor::is_compressed(v) {
                    match Compressor::decompress(v) {
                        Ok(decompressed) => {
                            stats.record_decompression(
                                &instance.name,
                                v.len() as u64,
                                decompressed.len() as u64,
                            );
                            new_values.push(Some(Bytes::from(decompressed)));
                        }
                        Err(e) => {
                            warn!("Decompression failed: {}", e);
                            new_values.push(Some(v.clone()));
                        }
                    }
                } else {
                    new_values.push(Some(v.clone()));
                }
            }
            None => new_values.push(None),
        }
    }

    DataRowMessage { values: new_values }
}

fn encode_compressed(data: &[u8]) -> String {
    use base64::{Engine as _, engine::general_purpose::STANDARD as BASE64};
    BASE64.encode(data)
}

fn query_type_to_str(query_type: QueryType) -> &'static str {
    match query_type {
        QueryType::Insert => "INSERT",
        QueryType::Update => "UPDATE",
        QueryType::Select => "SELECT",
        QueryType::Other => "OTHER",
    }
}
