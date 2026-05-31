use std::collections::HashMap;
use std::sync::Arc;
use parking_lot::RwLock;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TlsFingerprint {
    pub tls_version: u16,
    pub cipher_suites: Vec<u16>,
    pub extensions: Vec<u16>,
    pub elliptic_curves: Vec<u16>,
    pub ec_point_formats: Vec<u8>,
    pub signature_algorithms: Vec<u16>,
    pub session_id_len: usize,
    pub compression_methods: Vec<u8>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FingerprintFeature {
    pub cipher_suite_vector: Vec<f32>,
    pub extension_vector: Vec<f32>,
    pub elliptic_curve_vector: Vec<f32>,
    pub signature_algorithm_vector: Vec<f32>,
    pub compression_vector: Vec<f32>,
    pub tls_version: f32,
    pub session_id_len: f32,
}

pub struct FingerprintExtractor;

impl FingerprintExtractor {
    pub fn extract(data: &[u8]) -> Result<TlsFingerprint, String> {
        if data.len() < 5 {
            return Err("Insufficient data".to_string());
        }

        let record_type = data[0];
        if record_type != 0x16 {
            return Err("Not a TLS handshake".to_string());
        }

        let handshake_data = &data[5..];
        if handshake_data.is_empty() || handshake_data[0] != 0x01 {
            return Err("Not a ClientHello".to_string());
        }

        Self::parse_client_hello(handshake_data)
    }

    fn parse_client_hello(data: &[u8]) -> Result<TlsFingerprint, String> {
        if data.len() < 38 {
            return Err("ClientHello too short".to_string());
        }

        let tls_version = u16::from_be_bytes([data[4], data[5]]);
        let mut offset = 6;
        offset += 32;

        let session_id_len = data[offset] as usize;
        offset += 1 + session_id_len;

        if offset + 2 > data.len() {
            return Err("Invalid ClientHello structure".to_string());
        }

        let cipher_suites_len = u16::from_be_bytes([data[offset], data[offset + 1]]) as usize;
        offset += 2;

        if offset + cipher_suites_len > data.len() {
            return Err("Invalid cipher suites".to_string());
        }

        let mut cipher_suites = Vec::new();
        for chunk in data[offset..offset + cipher_suites_len].chunks(2) {
            if chunk.len() == 2 {
                cipher_suites.push(u16::from_be_bytes([chunk[0], chunk[1]]));
            }
        }
        offset += cipher_suites_len;

        if offset >= data.len() {
            return Ok(TlsFingerprint {
                tls_version,
                cipher_suites,
                extensions: vec![],
                elliptic_curves: vec![],
                ec_point_formats: vec![],
                signature_algorithms: vec![],
                session_id_len,
                compression_methods: vec![],
            });
        }

        let compression_len = data[offset] as usize;
        offset += 1 + compression_len;

        let mut compression_methods = Vec::new();
        for i in 0..compression_len {
            if offset + i < data.len() {
                compression_methods.push(data[offset + i]);
            }
        }
        offset += compression_len;

        if offset + 2 > data.len() {
            return Ok(TlsFingerprint {
                tls_version,
                cipher_suites,
                extensions: vec![],
                elliptic_curves: vec![],
                ec_point_formats: vec![],
                signature_algorithms: vec![],
                session_id_len,
                compression_methods,
            });
        }

        let extensions_len = u16::from_be_bytes([data[offset], data[offset + 1]]) as usize;
        offset += 2;

        if offset + extensions_len > data.len() {
            return Err("Invalid extensions".to_string());
        }

        let extensions_data = &data[offset..offset + extensions_len];
        let (extensions, elliptic_curves, ec_point_formats, signature_algorithms) = 
            Self::parse_extensions(extensions_data)?;

        Ok(TlsFingerprint {
            tls_version,
            cipher_suites,
            extensions,
            elliptic_curves,
            ec_point_formats,
            signature_algorithms,
            session_id_len,
            compression_methods,
        })
    }

    fn parse_extensions(data: &[u8]) -> Result<(Vec<u16>, Vec<u16>, Vec<u8>, Vec<u16>), String> {
        let mut extensions = Vec::new();
        let mut elliptic_curves = Vec::new();
        let mut ec_point_formats = Vec::new();
        let mut signature_algorithms = Vec::new();
        let mut offset = 0;

        while offset + 4 <= data.len() {
            let ext_type = u16::from_be_bytes([data[offset], data[offset + 1]]);
            let ext_len = u16::from_be_bytes([data[offset + 2], data[offset + 3]]) as usize;
            offset += 4;

            if offset + ext_len > data.len() {
                break;
            }

            extensions.push(ext_type);

            match ext_type {
                0x000A => {
                    elliptic_curves = Self::parse_elliptic_curves(&data[offset..offset + ext_len]);
                }
                0x000B => {
                    ec_point_formats = Self::parse_ec_point_formats(&data[offset..offset + ext_len]);
                }
                0x000D => {
                    signature_algorithms = Self::parse_signature_algorithms(&data[offset..offset + ext_len]);
                }
                _ => {}
            }

            offset += ext_len;
        }

        Ok((extensions, elliptic_curves, ec_point_formats, signature_algorithms))
    }

    fn parse_elliptic_curves(data: &[u8]) -> Vec<u16> {
        if data.len() < 2 {
            return vec![];
        }

        let curves_len = u16::from_be_bytes([data[0], data[1]]) as usize;
        if data.len() < 2 + curves_len {
            return vec![];
        }

        let mut curves = Vec::new();
        for chunk in data[2..2 + curves_len].chunks(2) {
            if chunk.len() == 2 {
                curves.push(u16::from_be_bytes([chunk[0], chunk[1]]));
            }
        }
        curves
    }

    fn parse_ec_point_formats(data: &[u8]) -> Vec<u8> {
        if data.is_empty() {
            return vec![];
        }

        let formats_len = data[0] as usize;
        if data.len() < 1 + formats_len {
            return vec![];
        }

        data[1..1 + formats_len].to_vec()
    }

    fn parse_signature_algorithms(data: &[u8]) -> Vec<u16> {
        if data.len() < 2 {
            return vec![];
        }

        let sig_len = u16::from_be_bytes([data[0], data[1]]) as usize;
        if data.len() < 2 + sig_len {
            return vec![];
        }

        let mut algorithms = Vec::new();
        for chunk in data[2..2 + sig_len].chunks(2) {
            if chunk.len() == 2 {
                algorithms.push(u16::from_be_bytes([chunk[0], chunk[1]]));
            }
        }
        algorithms
    }

    pub fn to_feature_vector(fingerprint: &TlsFingerprint) -> FingerprintFeature {
        FingerprintFeature {
            cipher_suite_vector: Self::one_hot_encode_cipher_suites(&fingerprint.cipher_suites),
            extension_vector: Self::one_hot_encode_extensions(&fingerprint.extensions),
            elliptic_curve_vector: Self::one_hot_encode_curves(&fingerprint.elliptic_curves),
            signature_algorithm_vector: Self::one_hot_encode_signatures(&fingerprint.signature_algorithms),
            compression_vector: Self::one_hot_encode_compression(&fingerprint.compression_methods),
            tls_version: fingerprint.tls_version as f32,
            session_id_len: fingerprint.session_id_len as f32,
        }
    }

    fn one_hot_encode_cipher_suites(suites: &[u16]) -> Vec<f32> {
        const NUM_CIPHER_SUITES: usize = 300;
        let mut vector = vec![0.0; NUM_CIPHER_SUITES];

        for (i, suite) in suites.iter().enumerate() {
            if *suite as usize < NUM_CIPHER_SUITES {
                vector[*suite as usize] = 1.0;
            } else if i < NUM_CIPHER_SUITES {
                vector[i] = 1.0;
            }
        }

        vector
    }

    fn one_hot_encode_extensions(exts: &[u16]) -> Vec<f32> {
        const NUM_EXTENSIONS: usize = 100;
        let mut vector = vec![0.0; NUM_EXTENSIONS];

        for (i, ext) in exts.iter().enumerate() {
            if *ext as usize < NUM_EXTENSIONS {
                vector[*ext as usize] = 1.0;
            } else if i < NUM_EXTENSIONS {
                vector[i] = 1.0;
            }
        }

        vector
    }

    fn one_hot_encode_curves(curves: &[u16]) -> Vec<f32> {
        const NUM_CURVES: usize = 30;
        let mut vector = vec![0.0; NUM_CURVES];

        for (i, curve) in curves.iter().enumerate() {
            if *curve as usize < NUM_CURVES {
                vector[*curve as usize] = 1.0;
            } else if i < NUM_CURVES {
                vector[i] = 1.0;
            }
        }

        vector
    }

    fn one_hot_encode_signatures(algs: &[u16]) -> Vec<f32> {
        const NUM_SIGNATURES: usize = 50;
        let mut vector = vec![0.0; NUM_SIGNATURES];

        for (i, alg) in algs.iter().enumerate() {
            if *alg as usize < NUM_SIGNATURES {
                vector[*alg as usize] = 1.0;
            } else if i < NUM_SIGNATURES {
                vector[i] = 1.0;
            }
        }

        vector
    }

    fn one_hot_encode_compression(comps: &[u8]) -> Vec<f32> {
        const NUM_COMPRESSIONS: usize = 10;
        let mut vector = vec![0.0; NUM_COMPRESSIONS];

        for comp in comps.iter() {
            if *comp as usize < NUM_COMPRESSIONS {
                vector[*comp as usize] = 1.0;
            }
        }

        vector
    }

    pub fn get_feature_dimensions() -> (usize, usize, usize, usize, usize) {
        (300, 100, 30, 50, 10)
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct KnownApplication {
    pub id: u32,
    pub name: String,
    pub category: String,
    pub confidence_threshold: f32,
}

pub struct ApplicationDatabase {
    applications: RwLock<HashMap<u32, KnownApplication>>,
    fingerprint_to_app: RwLock<HashMap<String, u32>>,
}

impl ApplicationDatabase {
    pub fn new() -> Self {
        Self {
            applications: RwLock::new(HashMap::new()),
            fingerprint_to_app: RwLock::new(HashMap::new()),
        }
    }

    pub fn load_from_file<P: AsRef<std::path::Path>>(&self, path: P) -> Result<usize, String> {
        let content = std::fs::read_to_string(path)
            .map_err(|e| format!("Failed to read file: {}", e))?;
        
        let apps: Vec<KnownApplication> = serde_json::from_str(&content)
            .map_err(|e| format!("Failed to parse JSON: {}", e))?;
        
        let mut applications = self.applications.write();
        let mut fingerprint_to_app = self.fingerprint_to_app.write();
        
        for app in apps {
            applications.insert(app.id, app.clone());
            let fp = format!("app_{}", app.id);
            fingerprint_to_app.insert(fp, app.id);
        }
        
        Ok(applications.len())
    }

    pub fn add_application(&self, app: KnownApplication) {
        self.applications.write().insert(app.id, app);
    }

    pub fn get_application(&self, id: u32) -> Option<KnownApplication> {
        self.applications.read().get(&id).cloned()
    }

    pub fn get_all_applications(&self) -> Vec<KnownApplication> {
        self.applications.read().values().cloned().collect()
    }
}

impl Default for ApplicationDatabase {
    fn default() -> Self {
        Self::new()
    }
}

pub struct FingerprintCache {
    cache: RwLock<lru::LruCache<String, CachedFingerprint>>,
    max_size: usize,
}

#[derive(Debug, Clone)]
pub struct CachedFingerprint {
    pub app_id: Option<u32>,
    pub app_name: Option<String>,
    pub confidence: f32,
    pub timestamp: std::time::Instant,
}

impl FingerprintCache {
    pub fn new(max_size: usize) -> Self {
        Self {
            cache: RwLock::new(lru::LruCache::new(max_size)),
            max_size,
        }
    }

    pub fn get(&self, key: &str) -> Option<CachedFingerprint> {
        self.cache.read().get(key).cloned()
    }

    pub fn put(&self, key: String, value: CachedFingerprint) {
        let mut cache = self.cache.write();
        if cache.len() >= self.max_size {
            cache.trim_to_size(self.max_size / 2);
        }
        cache.put(key, value);
    }

    pub fn clear(&self) {
        self.cache.write().clear();
    }

    pub fn len(&self) -> usize {
        self.cache.read().len()
    }
}
