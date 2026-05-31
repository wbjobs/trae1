use crate::stats::Stats;
use lru::LruCache;
use parking_lot::Mutex;
use std::num::NonZeroUsize;
use std::time::Instant;
use trust_dns_proto::op::Message;
use trust_dns_proto::rr::{Record, RecordType};

#[derive(Clone, Debug)]
pub struct CacheEntry {
    pub message: Message,
    pub inserted_at: Instant,
    pub ttl_secs: u32,
    #[allow(dead_code)]
    pub key: CacheKey,
}

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct CacheKey {
    pub name: String,
    pub record_type: RecordType,
}

pub struct Cache {
    inner: Mutex<LruCache<CacheKey, CacheEntry>>,
    stats: Stats,
    #[allow(dead_code)]
    max_entries: usize,
    prefetch_ratio: f32,
}

impl Cache {
    pub fn new(max_entries: usize, prefetch_ratio: f32, stats: Stats) -> Self {
        let cap = NonZeroUsize::new(max_entries.max(1)).unwrap();
        Self {
            inner: Mutex::new(LruCache::new(cap)),
            stats,
            max_entries,
            prefetch_ratio,
        }
    }

    #[allow(dead_code)]
    pub fn capacity(&self) -> usize {
        self.max_entries
    }

    #[allow(dead_code)]
    pub fn prefetch_ratio(&self) -> f32 {
        self.prefetch_ratio
    }

    #[allow(dead_code)]
    pub fn stats(&self) -> Stats {
        self.stats.clone()
    }

    pub fn get(&self, key: &CacheKey) -> Option<CacheEntry> {
        let mut guard = self.inner.lock();
        if let Some(entry) = guard.get(key).cloned() {
            if entry.inserted_at.elapsed().as_secs() < entry.ttl_secs as u64 {
                self.stats.hit();
                return Some(entry);
            } else {
                guard.pop_entry(key);
                self.stats.evict();
            }
        }
        self.stats.miss();
        None
    }

    pub fn should_prefetch(&self, entry: &CacheEntry) -> bool {
        let elapsed = entry.inserted_at.elapsed().as_secs() as f32;
        let ttl = entry.ttl_secs as f32;
        if ttl <= 0.0 {
            return false;
        }
        elapsed > ttl * (1.0 - self.prefetch_ratio)
    }

    pub fn insert(&self, key: CacheKey, message: Message) {
        let ttl = compute_message_ttl(&message);
        let entry = CacheEntry {
            message,
            inserted_at: Instant::now(),
            ttl_secs: ttl.max(1),
            key: key.clone(),
        };
        let mut guard = self.inner.lock();
        if guard.len() >= guard.cap().get() {
            self.stats.evict();
        }
        guard.put(key, entry);
        self.stats.insert();
        self.stats.set_entries(guard.len());
    }

    pub fn cleanup_expired(&self) -> usize {
        let mut guard = self.inner.lock();
        let before = guard.len();
        let expired: Vec<CacheKey> = guard
            .iter()
            .filter(|(_, e)| e.inserted_at.elapsed().as_secs() >= e.ttl_secs as u64)
            .map(|(k, _)| k.clone())
            .collect();
        for k in &expired {
            guard.pop_entry(k);
        }
        let removed = before - guard.len();
        for _ in 0..removed {
            self.stats.evict();
        }
        self.stats.set_entries(guard.len());
        removed
    }

    #[allow(dead_code)]
    pub fn len(&self) -> usize {
        self.inner.lock().len()
    }
}

pub fn compute_message_ttl(msg: &Message) -> u32 {
    let mut min_ttl: u32 = u32::MAX;
    for answers in [msg.answers(), msg.name_servers(), msg.additionals()] {
        for rec in answers {
            min_ttl = min_ttl.min(rec.ttl());
        }
    }
    if min_ttl == u32::MAX {
        300
    } else {
        min_ttl
    }
}

pub fn build_cache_key(name: &str, rtype: RecordType) -> CacheKey {
    CacheKey {
        name: name.to_lowercase(),
        record_type: rtype,
    }
}

pub fn message_key(msg: &Message) -> Option<CacheKey> {
    let query = msg.queries().first()?;
    Some(CacheKey {
        name: query.name().to_string().to_lowercase(),
        record_type: query.query_type(),
    })
}

pub fn apply_remaining_ttl(entry: &CacheEntry) -> Message {
    let remaining = (entry.ttl_secs as i64 - entry.inserted_at.elapsed().as_secs() as i64).max(0) as u32;
    let mut msg = entry.message.clone();
    let adjust = |recs: &mut [Record]| {
        for r in recs {
            let ttl = r.ttl().min(remaining);
            r.set_ttl(ttl);
        }
    };
    let answers: Vec<Record> = msg.answers().iter().cloned().collect();
    let ns: Vec<Record> = msg.name_servers().iter().cloned().collect();
    let adds: Vec<Record> = msg.additionals().iter().cloned().collect();
    let mut answers_mut = answers;
    let mut ns_mut = ns;
    let mut adds_mut = adds;
    adjust(&mut answers_mut);
    adjust(&mut ns_mut);
    adjust(&mut adds_mut);
    msg.insert_answers(answers_mut);
    msg.insert_name_servers(ns_mut);
    msg.insert_additionals(adds_mut);
    msg
}
