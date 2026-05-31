use std::net::IpAddr;
use thiserror::Error;
use trust_dns_resolver::config::{ResolverConfig, ResolverOpts};
use trust_dns_resolver::proto::rr::RecordType;
use trust_dns_resolver::{AsyncResolver, TokioAsyncResolver};

#[derive(Error, Debug)]
pub enum DnssecError {
    #[error("DNS resolution failed: {0}")]
    ResolutionFailed(String),
    #[error("DNSSEC validation failed")]
    ValidationFailed,
    #[error("No DNSSEC records found")]
    NoRecords,
    #[error("DNSSEC not supported by resolver")]
    NotSupported,
}

pub struct DnssecVerifier {
    resolver: TokioAsyncResolver,
    enabled: bool,
}

impl DnssecVerifier {
    pub fn new(enable_dnssec: bool) -> Result<Self, DnssecError> {
        let mut opts = ResolverOpts::default();
        opts.validate = enable_dnssec;
        opts.use_dnssec = enable_dnssec;

        let resolver = AsyncResolver::tokio(ResolverConfig::default(), opts)
            .map_err(|e| DnssecError::ResolutionFailed(e.to_string()))?;

        Ok(Self {
            resolver,
            enabled: enable_dnssec,
        })
    }

    pub async fn verify_domain(&self, domain: &str) -> Result<bool, DnssecError> {
        if !self.enabled {
            return Ok(true);
        }

        let lookup = self
            .resolver
            .lookup_ip(domain)
            .await
            .map_err(|e| DnssecError::ResolutionFailed(e.to_string()))?;

        let ips: Vec<IpAddr> = lookup.iter().collect();
        if ips.is_empty() {
            return Err(DnssecError::NoRecords);
        }

        Ok(true)
    }

    pub async fn verify_with_dnssec(&self, domain: &str) -> Result<DnssecResult, DnssecError> {
        if !self.enabled {
            return Ok(DnssecResult {
                domain: domain.to_string(),
                verified: true,
                ips: vec![],
                dnssec_valid: false,
            });
        }

        let lookup = self
            .resolver
            .lookup_ip(domain)
            .await
            .map_err(|e| DnssecError::ResolutionFailed(e.to_string()))?;

        let ips: Vec<IpAddr> = lookup.iter().collect();

        let dnssec_valid = self.check_dnssec(domain).await?;

        Ok(DnssecResult {
            domain: domain.to_string(),
            verified: dnssec_valid,
            ips,
            dnssec_valid,
        })
    }

    async fn check_dnssec(&self, domain: &str) -> Result<bool, DnssecError> {
        let rrsig_lookup = self
            .resolver
            .lookup(domain, RecordType::RRSIG)
            .await;

        match rrsig_lookup {
            Ok(_) => Ok(true),
            Err(_) => {
                let dnskey_lookup = self
                    .resolver
                    .lookup(domain, RecordType::DNSKEY)
                    .await;

                match dnskey_lookup {
                    Ok(_) => Ok(true),
                    Err(_) => Ok(false),
                }
            }
        }
    }

    pub async fn resolve_ip(&self, domain: &str) -> Result<Vec<IpAddr>, DnssecError> {
        let lookup = self
            .resolver
            .lookup_ip(domain)
            .await
            .map_err(|e| DnssecError::ResolutionFailed(e.to_string()))?;

        Ok(lookup.iter().collect())
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled
    }
}

#[derive(Debug, Clone)]
pub struct DnssecResult {
    pub domain: String,
    pub verified: bool,
    pub ips: Vec<IpAddr>,
    pub dnssec_valid: bool,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_dnssec_verifier_creation() {
        let verifier = DnssecVerifier::new(false);
        assert!(verifier.is_ok());
    }
}
