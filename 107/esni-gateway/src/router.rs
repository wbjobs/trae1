use ahash::AHashSet;
use parking_lot::RwLock;
use std::io::{BufRead, BufReader};
use std::path::Path;
use thiserror::Error;

#[derive(Error, Debug)]
pub enum RouterError {
    #[error("Failed to read list file: {0}")]
    FileReadError(#[from] std::io::Error),
    #[error("Invalid domain format: {0}")]
    InvalidDomain(String),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RoutingDecision {
    Allow,
    Block,
    Passthrough,
}

pub struct SniRouter {
    block_list: RwLock<AHashSet<String>>,
    allow_list: RwLock<AHashSet<String>>,
    whitelist_mode: bool,
}

impl SniRouter {
    pub fn new(whitelist_mode: bool) -> Self {
        Self {
            block_list: RwLock::new(AHashSet::new()),
            allow_list: RwLock::new(AHashSet::new()),
            whitelist_mode,
        }
    }

    pub fn load_block_list<P: AsRef<Path>>(&self, path: P) -> Result<usize, RouterError> {
        let file = std::fs::File::open(path)?;
        let reader = BufReader::new(file);
        let mut count = 0;
        let mut list = self.block_list.write();

        for line in reader.lines() {
            if let Ok(domain) = line {
                let domain = domain.trim().to_lowercase();
                if !domain.is_empty() && !domain.starts_with('#') {
                    list.insert(domain);
                    count += 1;
                }
            }
        }

        Ok(count)
    }

    pub fn load_allow_list<P: AsRef<Path>>(&self, path: P) -> Result<usize, RouterError> {
        let file = std::fs::File::open(path)?;
        let reader = BufReader::new(file);
        let mut count = 0;
        let mut list = self.allow_list.write();

        for line in reader.lines() {
            if let Ok(domain) = line {
                let domain = domain.trim().to_lowercase();
                if !domain.is_empty() && !domain.starts_with('#') {
                    list.insert(domain);
                    count += 1;
                }
            }
        }

        Ok(count)
    }

    pub fn add_to_block_list(&self, domain: String) {
        self.block_list.write().insert(domain.to_lowercase());
    }

    pub fn add_to_allow_list(&self, domain: String) {
        self.allow_list.write().insert(domain.to_lowercase());
    }

    pub fn remove_from_block_list(&self, domain: &str) -> bool {
        self.block_list.write().remove(&domain.to_lowercase())
    }

    pub fn remove_from_allow_list(&self, domain: &str) -> bool {
        self.allow_list.write().remove(&domain.to_lowercase())
    }

    pub fn make_decision(&self, sni: Option<&str>) -> RoutingDecision {
        match sni {
            Some(domain) => {
                let domain = domain.to_lowercase();

                if self.whitelist_mode {
                    let allow_list = self.allow_list.read();
                    if allow_list.contains(&domain) || self.matches_wildcard(&domain, &allow_list) {
                        return RoutingDecision::Allow;
                    }
                    return RoutingDecision::Block;
                }

                let block_list = self.block_list.read();
                if block_list.contains(&domain) || self.matches_wildcard(&domain, &block_list) {
                    return RoutingDecision::Block;
                }

                RoutingDecision::Allow
            }
            None => {
                if self.whitelist_mode {
                    RoutingDecision::Block
                } else {
                    RoutingDecision::Passthrough
                }
            }
        }
    }

    fn matches_wildcard(&self, domain: &str, list: &AHashSet<String>) -> bool {
        let parts: Vec<&str> = domain.split('.').collect();

        for i in 0..parts.len() {
            let wildcard = format!("*.{}", parts[i..].join("."));
            if list.contains(&wildcard) {
                return true;
            }
        }

        false
    }

    pub fn get_block_list_size(&self) -> usize {
        self.block_list.read().len()
    }

    pub fn get_allow_list_size(&self) -> usize {
        self.allow_list.read().len()
    }

    pub fn is_whitelist_mode(&self) -> bool {
        self.whitelist_mode
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_router_decision() {
        let router = SniRouter::new(false);
        router.add_to_block_list("evil.com".to_string());

        assert_eq!(
            router.make_decision(Some("evil.com")),
            RoutingDecision::Block
        );
        assert_eq!(
            router.make_decision(Some("good.com")),
            RoutingDecision::Allow
        );
    }

    #[test]
    fn test_whitelist_mode() {
        let router = SniRouter::new(true);
        router.add_to_allow_list("allowed.com".to_string());

        assert_eq!(
            router.make_decision(Some("allowed.com")),
            RoutingDecision::Allow
        );
        assert_eq!(
            router.make_decision(Some("blocked.com")),
            RoutingDecision::Block
        );
    }

    #[test]
    fn test_wildcard_matching() {
        let router = SniRouter::new(false);
        router.add_to_block_list("*.evil.com".to_string());

        assert_eq!(
            router.make_decision(Some("sub.evil.com")),
            RoutingDecision::Block
        );
        assert_eq!(
            router.make_decision(Some("deep.sub.evil.com")),
            RoutingDecision::Block
        );
    }
}
