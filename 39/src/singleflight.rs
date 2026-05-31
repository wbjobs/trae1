use std::sync::Arc;

use dashmap::DashMap;
use tokio::sync::Notify;

#[derive(Clone)]
pub struct Singleflight {
    in_flight: DashMap<(String, String), Arc<Notify>>,
}

impl Singleflight {
    pub fn new() -> Self {
        Self {
            in_flight: DashMap::new(),
        }
    }

    pub fn is_in_flight(&self, url_path: &str, algo: &str) -> bool {
        self.in_flight
            .contains_key(&(url_path.to_string(), algo.to_string()))
    }

    pub fn try_acquire(&self, url_path: &str, algo: &str) -> Option<Arc<Notify>> {
        let key = (url_path.to_string(), algo.to_string());
        if self.in_flight.contains_key(&key) {
            return None;
        }
        let notify = Arc::new(Notify::new());
        self.in_flight.insert(key, notify.clone());
        Some(notify)
    }

    pub fn finish(&self, url_path: &str, algo: &str) {
        let key = (url_path.to_string(), algo.to_string());
        if let Some((_, notify)) = self.in_flight.remove(&key) {
            notify.notify_waiters();
        }
    }

    pub fn in_flight_count(&self) -> usize {
        self.in_flight.len()
    }
}
