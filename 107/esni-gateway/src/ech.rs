use thiserror::Error;
use std::sync::Arc;
use parking_lot::RwLock;
use std::time::{Duration, Instant};
use std::path::Path;
use x25519_dalek::{PublicKey, StaticSecret, EphemeralSecret};
use rand::rngs::OsRng;
use hkdf::Hkdf;
use sha2::Sha256;
use aes_gcm::{Aes128Gcm, Aes256Gcm, KeyInit, Nonce};
use chacha20poly1305::ChaCha20Poly1305;
use serde::{Deserialize, Serialize};
use bytes::Bytes;

#[derive(Error, Debug)]
pub enum EchError {
    #[error("ECH extension not found")]
    ExtensionNotFound,
    #[error("Invalid ECH extension format")]
    InvalidFormat,
    #[error("Unsupported ECH version: {0}")]
    UnsupportedVersion(u16),
    #[error("No matching key found for key ID: {0}")]
    KeyNotFound(Vec<u8>),
    #[error("Decryption failed")]
    DecryptionFailed,
    #[error("Invalid cipher suite: {0}")]
    InvalidCipherSuite(u16),
    #[error("Key derivation failed")]
    KeyDerivationFailed,
    #[error("Failed to load keys: {0}")]
    KeyLoadError(String),
    #[error("Serialization error: {0}")]
    SerializationError(String),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EchKeyPair {
    pub key_id: Vec<u8>,
    pub private_key: Vec<u8>,
    pub public_key: Vec<u8>,
    pub valid_from: u64,
    pub valid_until: u64,
    pub kdf_id: u16,
    pub aead_id: u16,
    pub maximum_name_length: u16,
    pub public_name: String,
}

#[derive(Debug, Clone)]
pub struct EchDecryptionResult {
    pub sni: Option<String>,
    pub public_name: String,
    pub inner_client_hello: Option<Vec<u8>>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EchConfig {
    pub key_pairs: Vec<EchKeyPair>,
    pub key_rotation_interval: u64,
    pub fallback_enabled: bool,
}

impl Default for EchConfig {
    fn default() -> Self {
        Self {
            key_pairs: Vec::new(),
            key_rotation_interval: 3600,
            fallback_enabled: true,
        }
    }
}

pub struct EchDecryptor {
    config: EchConfig,
    keys: RwLock<Vec<Arc<EchKeyPair>>>,
    last_rotation: RwLock<Instant>,
    fallback_enabled: bool,
}

impl EchDecryptor {
    pub fn new(config: EchConfig) -> Self {
        let keys: Vec<Arc<EchKeyPair>> = config.key_pairs.iter()
            .map(|kp| Arc::new(kp.clone()))
            .collect();
        
        Self {
            config,
            keys: RwLock::new(keys),
            last_rotation: RwLock::new(Instant::now()),
            fallback_enabled: config.fallback_enabled,
        }
    }

    pub fn from_file<P: AsRef<Path>>(path: P) -> Result<Self, EchError> {
        let content = std::fs::read_to_string(path)
            .map_err(|e| EchError::KeyLoadError(e.to_string()))?;
        
        let config: EchConfig = serde_json::from_str(&content)
            .map_err(|e| EchError::SerializationError(e.to_string()))?;
        
        Ok(Self::new(config))
    }

    pub fn add_key(&self, key: EchKeyPair) {
        self.keys.write().push(Arc::new(key));
    }

    pub fn remove_expired_keys(&self) {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs();
        
        let mut keys = self.keys.write();
        keys.retain(|kp| kp.valid_until > now);
    }

    pub fn rotate_keys(&self, new_keys: Vec<EchKeyPair>) {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs();
        
        let valid_keys: Vec<Arc<EchKeyPair>> = self.keys.read()
            .iter()
            .filter(|kp| kp.valid_until > now)
            .cloned()
            .collect();
        
        let mut updated_keys = valid_keys;
        for key in new_keys {
            updated_keys.push(Arc::new(key));
        }
        
        *self.keys.write() = updated_keys;
        *self.last_rotation.write() = Instant::now();
    }

    pub fn should_rotate(&self) -> bool {
        self.last_rotation.read().elapsed() > 
            Duration::from_secs(self.config.key_rotation_interval)
    }

    pub fn decrypt_ech(&self, ech_extension: &[u8]) -> Result<EchDecryptionResult, EchError> {
        if ech_extension.len() < 6 {
            return Err(EchError::InvalidFormat);
        }

        let version = u16::from_be_bytes([ech_extension[0], ech_extension[1]]);
        
        match version {
            0x0000 | 0x0001 | 0x0002 | 0xFE09 => {
                self.decrypt_ech_v1(ech_extension)
            }
            _ => Err(EchError::UnsupportedVersion(version))
        }
    }

    fn decrypt_ech_v1(&self, ech_data: &[u8]) -> Result<EchDecryptionResult, EchError> {
        let mut offset = 2;
        
        let config_id_len = ech_data[offset] as usize;
        offset += 1;
        
        if offset + config_id_len > ech_data.len() {
            return Err(EchError::InvalidFormat);
        }
        
        let config_id = &ech_data[offset..offset + config_id_len];
        offset += config_id_len;
        
        let key_pair = self.find_key_by_id(config_id)
            .ok_or_else(|| EchError::KeyNotFound(config_id.to_vec()))?;
        
        let enc_len = u16::from_be_bytes([ech_data[offset], ech_data[offset + 1]]) as usize;
        offset += 2;
        
        if offset + enc_len > ech_data.len() {
            return Err(EchError::InvalidFormat);
        }
        
        let encrypted_ch = &ech_data[offset..offset + enc_len];
        
        let decrypted = self.perform_decryption(
            &key_pair,
            encrypted_ch,
            config_id
        )?;
        
        let (sni, inner_ch) = self.parse_decrypted_inner_hello(&decrypted)?;
        
        Ok(EchDecryptionResult {
            sni,
            public_name: key_pair.public_name.clone(),
            inner_client_hello: Some(inner_ch),
        })
    }

    fn find_key_by_id(&self, key_id: &[u8]) -> Option<Arc<EchKeyPair>> {
        self.keys.read()
            .iter()
            .find(|kp| kp.key_id == key_id)
            .cloned()
    }

    fn perform_decryption(
        &self,
        key_pair: &EchKeyPair,
        encrypted: &[u8],
        config_id: &[u8],
    ) -> Result<Vec<u8>, EchError> {
        if encrypted.len() < 32 + 12 {
            return Err(EchError::InvalidFormat);
        }

        let client_public_key = &encrypted[0..32];
        let nonce = &encrypted[32..44];
        let ciphertext = &encrypted[44..];

        let shared_secret = self.compute_shared_secret(&key_pair.private_key, client_public_key)?;
        
        let key = self.derive_keys(&shared_secret, config_id, key_pair)?;
        
        let plaintext = match key_pair.aead_id {
            0x0001 => self.decrypt_aes_128_gcm(&key, nonce, ciphertext)?,
            0x0002 => self.decrypt_aes_256_gcm(&key, nonce, ciphertext)?,
            0x0003 => self.decrypt_chacha20_poly1305(&key, nonce, ciphertext)?,
            _ => return Err(EchError::InvalidCipherSuite(key_pair.aead_id)),
        };
        
        Ok(plaintext)
    }

    fn compute_shared_secret(
        &self,
        private_key: &[u8],
        public_key: &[u8],
    ) -> Result<[u8; 32], EchError> {
        let secret_key = StaticSecret::from(<[u8; 32]>::try_from(private_key)
            .map_err(|_| EchError::DecryptionFailed)?);
        
        let public_key = PublicKey::from(<[u8; 32]>::try_from(public_key)
            .map_err(|_| EchError::DecryptionFailed)?);
        
        Ok(secret_key.diffie_hellman(&public_key).to_bytes())
    }

    fn derive_keys(
        &self,
        shared_secret: &[u8; 32],
        config_id: &[u8],
        key_pair: &EchKeyPair,
    ) -> Result<Vec<u8>, EchError> {
        let hkdf = Hkdf::<Sha256>::new(None, shared_secret);
        
        let mut info = Vec::new();
        info.extend_from_slice(b"ech hpke key");
        info.extend_from_slice(config_id);
        
        let mut key = vec![0u8; 32];
        hkdf.expand(&info, &mut key)
            .map_err(|_| EchError::KeyDerivationFailed)?;
        
        Ok(key)
    }

    fn decrypt_aes_128_gcm(
        &self,
        key: &[u8],
        nonce: &[u8],
        ciphertext: &[u8],
    ) -> Result<Vec<u8>, EchError> {
        let cipher = Aes128Gcm::new_from_slice(&key[0..16])
            .map_err(|_| EchError::DecryptionFailed)?;
        
        let nonce = Nonce::from_slice(nonce);
        
        cipher.decrypt(nonce, ciphertext)
            .map_err(|_| EchError::DecryptionFailed)
    }

    fn decrypt_aes_256_gcm(
        &self,
        key: &[u8],
        nonce: &[u8],
        ciphertext: &[u8],
    ) -> Result<Vec<u8>, EchError> {
        let cipher = Aes256Gcm::new_from_slice(key)
            .map_err(|_| EchError::DecryptionFailed)?;
        
        let nonce = Nonce::from_slice(nonce);
        
        cipher.decrypt(nonce, ciphertext)
            .map_err(|_| EchError::DecryptionFailed)
    }

    fn decrypt_chacha20_poly1305(
        &self,
        key: &[u8],
        nonce: &[u8],
        ciphertext: &[u8],
    ) -> Result<Vec<u8>, EchError> {
        let cipher = ChaCha20Poly1305::new_from_slice(key)
            .map_err(|_| EchError::DecryptionFailed)?;
        
        let nonce = Nonce::from_slice(nonce);
        
        cipher.decrypt(nonce, ciphertext)
            .map_err(|_| EchError::DecryptionFailed)
    }

    fn parse_decrypted_inner_hello(
        &self,
        data: &[u8],
    ) -> Result<(Option<String>, Vec<u8>), EchError> {
        if data.len() < 4 {
            return Ok((None, data.to_vec()));
        }

        let inner_len = u32::from_be_bytes([data[0], data[1], data[2], data[3]]) as usize;
        if data.len() < 4 + inner_len {
            return Ok((None, data.to_vec()));
        }

        let inner_ch = &data[4..4 + inner_len];
        
        let sni = self.extract_sni_from_inner_hello(inner_ch).ok();
        
        Ok((sni, inner_ch.to_vec()))
    }

    fn extract_sni_from_inner_hello(&self, data: &[u8]) -> Result<String, EchError> {
        if data.len() < 5 {
            return Err(EchError::InvalidFormat);
        }

        let record_type = data[0];
        if record_type != 0x16 {
            return Err(EchError::InvalidFormat);
        }

        let version = u16::from_be_bytes([data[1], data[2]]);
        let length = u16::from_be_bytes([data[3], data[4]]) as usize;

        if data.len() < 5 + length {
            return Err(EchError::InvalidFormat);
        }

        let handshake_data = &data[5..5 + length];
        
        if handshake_data.is_empty() || handshake_data[0] != 0x01 {
            return Err(EchError::InvalidFormat);
        }

        self.parse_sni_from_handshake(&handshake_data[1..])
    }

    fn parse_sni_from_handshake(&self, data: &[u8]) -> Result<String, EchError> {
        if data.len() < 38 {
            return Err(EchError::InvalidFormat);
        }

        let mut offset = 0;
        let _handshake_len = u32::from_be_bytes([0, data[offset], data[offset + 1], data[offset + 2]]) as usize;
        offset += 4;
        offset += 2;
        offset += 32;

        if offset >= data.len() {
            return Err(EchError::InvalidFormat);
        }

        let session_id_len = data[offset] as usize;
        offset += 1 + session_id_len;

        if offset + 2 >= data.len() {
            return Err(EchError::InvalidFormat);
        }

        let cipher_suites_len = u16::from_be_bytes([data[offset], data[offset + 1]]) as usize;
        offset += 2 + cipher_suites_len;

        if offset >= data.len() {
            return Err(EchError::InvalidFormat);
        }

        let compression_len = data[offset] as usize;
        offset += 1 + compression_len;

        if offset + 2 >= data.len() {
            return Err(EchError::InvalidFormat);
        }

        let extensions_len = u16::from_be_bytes([data[offset], data[offset + 1]]) as usize;
        offset += 2;

        if offset + extensions_len > data.len() {
            return Err(EchError::InvalidFormat);
        }

        let extensions = &data[offset..offset + extensions_len];
        
        self.find_sni_in_extensions(extensions)
    }

    fn find_sni_in_extensions(&self, extensions: &[u8]) -> Result<String, EchError> {
        let mut offset = 0;
        
        while offset + 4 <= extensions.len() {
            let ext_type = u16::from_be_bytes([extensions[offset], extensions[offset + 1]]);
            let ext_len = u16::from_be_bytes([extensions[offset + 2], extensions[offset + 3]]) as usize;
            offset += 4;

            if offset + ext_len > extensions.len() {
                break;
            }

            if ext_type == 0x0000 {
                return self.parse_sni_extension(&extensions[offset..offset + ext_len]);
            }

            offset += ext_len;
        }

        Err(EchError::ExtensionNotFound)
    }

    fn parse_sni_extension(&self, data: &[u8]) -> Result<String, EchError> {
        if data.len() < 2 {
            return Err(EchError::InvalidFormat);
        }

        let list_len = u16::from_be_bytes([data[0], data[1]]) as usize;
        if data.len() < 2 + list_len {
            return Err(EchError::InvalidFormat);
        }

        let mut offset = 2;
        while offset + 3 <= data.len() {
            let name_type = data[offset];
            let name_len = u16::from_be_bytes([data[offset + 1], data[offset + 2]]) as usize;
            offset += 3;

            if offset + name_len > data.len() {
                break;
            }

            if name_type == 0 {
                let name = String::from_utf8(data[offset..offset + name_len].to_vec())
                    .map_err(|_| EchError::InvalidFormat)?;
                return Ok(name);
            }

            offset += name_len;
        }

        Err(EchError::ExtensionNotFound)
    }

    pub fn is_enabled(&self) -> bool {
        !self.keys.read().is_empty()
    }

    pub fn get_key_count(&self) -> usize {
        self.keys.read().len()
    }

    pub fn is_fallback_enabled(&self) -> bool {
        self.fallback_enabled
    }

    pub fn generate_key_pair(
        public_name: String,
        lifetime_hours: u64,
    ) -> Result<EchKeyPair, EchError> {
        let mut rng = OsRng;
        let private_key = StaticSecret::random_from_rng(&mut rng);
        let public_key = PublicKey::from(&private_key);
        
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs();
        
        let mut key_id = vec![0u8; 16];
        rng.fill_bytes(&mut key_id);
        
        Ok(EchKeyPair {
            key_id,
            private_key: private_key.to_bytes().to_vec(),
            public_key: public_key.as_bytes().to_vec(),
            valid_from: now,
            valid_until: now + lifetime_hours * 3600,
            kdf_id: 0x0001,
            aead_id: 0x0002,
            maximum_name_length: 255,
            public_name,
        })
    }
}

pub struct EchFailureLogger {
    log_file: Option<std::fs::File>,
}

impl EchFailureLogger {
    pub fn new<P: AsRef<Path>>(path: Option<P>) -> std::io::Result<Self> {
        let log_file = match path {
            Some(p) => {
                let file = std::fs::OpenOptions::new()
                    .create(true)
                    .append(true)
                    .open(p)?;
                Some(file)
            }
            None => None,
        };
        
        Ok(Self { log_file })
    }

    pub fn log_failure(
        &self,
        client_addr: &std::net::SocketAddr,
        error: &EchError,
        ech_data: &[u8],
    ) {
        if let Some(mut file) = &self.log_file {
            use std::io::Write;
            
            let timestamp = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_secs();
            
            let entry = format!(
                "[{}] Client: {}, Error: {:?}, ECH Data Len: {}\n",
                timestamp,
                client_addr,
                error,
                ech_data.len()
            );
            
            let _ = file.write_all(entry.as_bytes());
            let _ = file.flush();
        }
    }
}

impl Drop for EchFailureLogger {
    fn drop(&mut self) {
        if let Some(mut file) = &self.log_file {
            use std::io::Write;
            let _ = file.flush();
        }
    }
}
