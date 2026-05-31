use parking_lot::RwLock;
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::net::TcpStream;
use tokio::sync::Mutex;

#[derive(Debug, Clone)]
pub struct ConnectionInfo {
    pub addr: SocketAddr,
    pub created_at: Instant,
    pub last_used: Instant,
    pub active: bool,
}

pub struct ConnectionPool {
    connections: RwLock<HashMap<SocketAddr, Vec<Arc<Mutex<Option<TcpStream>>>>>>,
    max_connections_per_host: usize,
    idle_timeout: Duration,
}

impl ConnectionPool {
    pub fn new(max_connections_per_host: usize, idle_timeout: Duration) -> Self {
        Self {
            connections: RwLock::new(HashMap::new()),
            max_connections_per_host,
            idle_timeout,
        }
    }

    pub async fn get_connection(&self, addr: SocketAddr) -> std::io::Result<TcpStream> {
        let mut connections = self.connections.write();

        if let Some(pool) = connections.get_mut(&addr) {
            for conn in pool.iter_mut().rev() {
                let mut conn_guard = conn.lock().await;
                if let Some(stream) = conn_guard.take() {
                    return Ok(stream);
                }
            }
        }

        drop(connections);
        TcpStream::connect(addr).await
    }

    pub async fn return_connection(&self, addr: SocketAddr, stream: TcpStream) {
        let mut connections = self.connections.write();

        let pool = connections.entry(addr).or_insert_with(Vec::new);

        if pool.len() < self.max_connections_per_host {
            pool.push(Arc::new(Mutex::new(Some(stream))));
        }
    }

    pub fn cleanup_idle(&self) {
        let mut connections = self.connections.write();
        let now = Instant::now();

        for (_, pool) in connections.iter_mut() {
            pool.retain(|conn| {
                if let Some(stream) = conn.try_lock() {
                    if stream.is_some() {
                        return true;
                    }
                }
                false
            });
        }
    }

    pub fn stats(&self) -> PoolStats {
        let connections = self.connections.read();
        let mut total_connections = 0;
        let mut total_hosts = connections.len();

        for pool in connections.values() {
            total_connections += pool.len();
        }

        PoolStats {
            total_connections,
            total_hosts,
        }
    }
}

#[derive(Debug, Clone)]
pub struct PoolStats {
    pub total_connections: usize,
    pub total_hosts: usize,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_connection_pool_creation() {
        let pool = ConnectionPool::new(10, Duration::from_secs(30));
        let stats = pool.stats();
        assert_eq!(stats.total_connections, 0);
        assert_eq!(stats.total_hosts, 0);
    }
}
