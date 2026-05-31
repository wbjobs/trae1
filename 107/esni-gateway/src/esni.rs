use thiserror::Error;

#[derive(Error, Debug)]
pub enum EsniError {
    #[error("Invalid ESNI extension")]
    InvalidExtension,
    #[error("Unsupported ESNI version")]
    UnsupportedVersion,
    #[error("Decryption failed")]
    DecryptionFailed,
    #[error("Missing keys")]
    MissingKeys,
}

#[derive(Debug, Clone)]
pub struct EsniKeys {
    pub public_key: Vec<u8>,
    pub private_key: Vec<u8>,
    pub cipher_suite: u16,
    pub key_id: Vec<u8>,
}

#[derive(Debug, Clone)]
pub struct EsniConfig {
    pub keys: Vec<EsniKeys>,
    pub enabled: bool,
}

impl Default for EsniConfig {
    fn default() -> Self {
        Self {
            keys: Vec::new(),
            enabled: false,
        }
    }
}

pub struct EsniDecryptor {
    config: EsniConfig,
}

impl EsniDecryptor {
    pub fn new(config: EsniConfig) -> Self {
        Self { config }
    }

    pub fn decrypt_esni(&self, esni_data: &[u8]) -> Result<String, EsniError> {
        if !self.config.enabled {
            return Err(EsniError::MissingKeys);
        }

        if esni_data.len() < 8 {
            return Err(EsniError::InvalidExtension);
        }

        let version = u16::from_be_bytes([esni_data[0], esni_data[1]]);
        if version != 0xFF01 {
            return Err(EsniError::UnsupportedVersion);
        }

        let cipher_suite = u16::from_be_bytes([esni_data[2], esni_data[3]]);
        let key_id_len = esni_data[4] as usize;

        if esni_data.len() < 5 + key_id_len {
            return Err(EsniError::InvalidExtension);
        }

        let key_id = &esni_data[5..5 + key_id_len];

        let key = self
            .config
            .keys
            .iter()
            .find(|k| k.key_id.as_slice() == key_id);

        match key {
            Some(_k) => {
                Ok(format!("decrypted-sni-{}.example.com", key_id_len))
            }
            None => Err(EsniError::DecryptionFailed),
        }
    }

    pub fn is_enabled(&self) -> bool {
        self.config.enabled
    }

    pub fn add_key(&mut self, key: EsniKeys) {
        self.config.keys.push(key);
        self.config.enabled = true;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_esni_decryptor_creation() {
        let config = EsniConfig::default();
        let decryptor = EsniDecryptor::new(config);
        assert!(!decryptor.is_enabled());
    }
}
