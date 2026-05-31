use bytes::{Buf, BytesMut};
use thiserror::Error;

#[derive(Error, Debug)]
pub enum TlsParseError {
    #[error("Insufficient data")]
    InsufficientData,
    #[error("Invalid TLS record")]
    InvalidRecord,
    #[error("Invalid handshake")]
    InvalidHandshake,
    #[error("No SNI found")]
    NoSni,
    #[error("Invalid SNI extension")]
    InvalidSniExtension,
}

#[derive(Debug, Clone)]
pub struct ClientHelloInfo {
    pub sni: Option<String>,
    pub ech: Option<Vec<u8>>,
    pub esni: Option<Vec<u8>>,
    pub tls_version: u16,
    pub cipher_suites: Vec<u16>,
    pub extensions: Vec<(u16, Vec<u8>)>,
}

pub struct TlsParser;

impl TlsParser {
    pub fn parse_client_hello(data: &[u8]) -> Result<ClientHelloInfo, TlsParseError> {
        if data.len() < 5 {
            return Err(TlsParseError::InsufficientData);
        }

        let record_type = data[0];
        if record_type != 0x16 {
            return Err(TlsParseError::InvalidRecord);
        }

        let version = u16::from_be_bytes([data[1], data[2]]);
        let length = u16::from_be_bytes([data[3], data[4]]) as usize;

        if data.len() < 5 + length {
            return Err(TlsParseError::InsufficientData);
        }

        let handshake_data = &data[5..5 + length];

        if handshake_data.is_empty() {
            return Err(TlsParseError::InvalidHandshake);
        }

        let handshake_type = handshake_data[0];
        if handshake_type != 0x01 {
            return Err(TlsParseError::InvalidHandshake);
        }

        Self::parse_handshake(handshake_data)
    }

    fn parse_handshake(data: &[u8]) -> Result<ClientHelloInfo, TlsParseError> {
        if data.len() < 38 {
            return Err(TlsParseError::InsufficientData);
        }

        let _handshake_length = u32::from_be_bytes([0, data[1], data[2], data[3]]) as usize;
        let tls_version = u16::from_be_bytes([data[4], data[5]]);

        let mut offset = 6;
        offset += 32;

        let session_id_len = data[offset] as usize;
        offset += 1 + session_id_len;

        if offset + 2 > data.len() {
            return Err(TlsParseError::InsufficientData);
        }

        let cipher_suites_len = u16::from_be_bytes([data[offset], data[offset + 1]]) as usize;
        offset += 2;

        if offset + cipher_suites_len > data.len() {
            return Err(TlsParseError::InsufficientData);
        }

        let mut cipher_suites = Vec::new();
        let cipher_data = &data[offset..offset + cipher_suites_len];
        for chunk in cipher_data.chunks(2) {
            if chunk.len() == 2 {
                cipher_suites.push(u16::from_be_bytes([chunk[0], chunk[1]]));
            }
        }
        offset += cipher_suites_len;

        if offset >= data.len() {
            return Ok(ClientHelloInfo {
                sni: None,
                ech: None,
                esni: None,
                tls_version,
                cipher_suites,
                extensions: Vec::new(),
            });
        }

        let compression_len = data[offset] as usize;
        offset += 1 + compression_len;

        if offset + 2 > data.len() {
            return Ok(ClientHelloInfo {
                sni: None,
                ech: None,
                esni: None,
                tls_version,
                cipher_suites,
                extensions: Vec::new(),
            });
        }

        let extensions_len = u16::from_be_bytes([data[offset], data[offset + 1]]) as usize;
        offset += 2;

        if offset + extensions_len > data.len() {
            return Err(TlsParseError::InsufficientData);
        }

        let extensions_data = &data[offset..offset + extensions_len];
        let (extensions, sni, ech, esni) = Self::parse_extensions(extensions_data)?;

        Ok(ClientHelloInfo {
            sni,
            ech,
            esni,
            tls_version,
            cipher_suites,
            extensions,
        })
    }

    fn parse_extensions(
        data: &[u8],
    ) -> Result<(Vec<(u16, Vec<u8>)>, Option<String>, Option<Vec<u8>>, Option<Vec<u8>>), TlsParseError> {
        let mut extensions = Vec::new();
        let mut sni = None;
        let mut ech = None;
        let mut esni = None;
        let mut offset = 0;

        while offset + 4 <= data.len() {
            let ext_type = u16::from_be_bytes([data[offset], data[offset + 1]]);
            let ext_len = u16::from_be_bytes([data[offset + 2], data[offset + 3]]) as usize;
            offset += 4;

            if offset + ext_len > data.len() {
                break;
            }

            let ext_data = data[offset..offset + ext_len].to_vec();

            if ext_type == 0x0000 {
                sni = Self::parse_sni_extension(&ext_data)?;
            } else if ext_type == 0xFE0D {
                // Legacy ESNI extension
                esni = Some(ext_data.clone());
            } else if ext_type == 0xCE00 {
                // Modern ECH extension
                ech = Some(ext_data.clone());
            }

            extensions.push((ext_type, ext_data));
            offset += ext_len;
        }

        Ok((extensions, sni, ech, esni))
    }

    fn parse_sni_extension(data: &[u8]) -> Result<Option<String>, TlsParseError> {
        if data.len() < 2 {
            return Err(TlsParseError::InvalidSniExtension);
        }

        let list_len = u16::from_be_bytes([data[0], data[1]]) as usize;
        if data.len() < 2 + list_len {
            return Err(TlsParseError::InvalidSniExtension);
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
                let name = String::from_utf8(data[offset..offset + name_len].to_vec());
                if let Ok(hostname) = name {
                    return Ok(Some(hostname));
                }
            }

            offset += name_len;
        }

        Ok(None)
    }

    pub fn extract_sni_from_buffer(buf: &BytesMut) -> Result<(Option<String>, Option<Vec<u8>>), TlsParseError> {
        if buf.len() < 5 {
            return Err(TlsParseError::InsufficientData);
        }

        let record_type = buf[0];
        if record_type != 0x16 {
            return Err(TlsParseError::InvalidRecord);
        }

        let length = u16::from_be_bytes([buf[3], buf[4]]) as usize;
        if buf.len() < 5 + length {
            return Err(TlsParseError::InsufficientData);
        }

        match Self::parse_client_hello(&buf[..5 + length]) {
            Ok(info) => {
                let ech_data = info.ech.or(info.esni);
                Ok((info.sni, ech_data))
            },
            Err(e) => Err(e),
        }
    }
}
