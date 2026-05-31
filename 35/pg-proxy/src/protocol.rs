use bytes::{Buf, BufMut, Bytes, BytesMut};
use std::io;

pub const MSG_TYPE_QUERY: u8 = b'Q';
pub const MSG_TYPE_PARSE: u8 = b'P';
pub const MSG_TYPE_BIND: u8 = b'B';
pub const MSG_TYPE_EXECUTE: u8 = b'E';
pub const MSG_TYPE_DESCRIBE: u8 = b'D';
pub const MSG_TYPE_SYNC: u8 = b'S';
pub const MSG_TYPE_FLUSH: u8 = b'H';
pub const MSG_TYPE_CLOSE: u8 = b'C';

pub const MSG_TYPE_AUTH_REQUEST: u8 = b'R';
pub const MSG_TYPE_PARAMETER_STATUS: u8 = b'S';
pub const MSG_TYPE_BACKEND_KEY: u8 = b'K';
pub const MSG_TYPE_READY_FOR_QUERY: u8 = b'Z';
pub const MSG_TYPE_ROW_DESCRIPTION: u8 = b'T';
pub const MSG_TYPE_DATA_ROW: u8 = b'D';
pub const MSG_TYPE_COMMAND_COMPLETE: u8 = b'C';
pub const MSG_TYPE_ERROR_RESPONSE: u8 = b'E';
pub const MSG_TYPE_NOTICE_RESPONSE: u8 = b'N';
pub const MSG_TYPE_EMPTY_QUERY: u8 = b'I';
pub const MSG_TYPE_NO_DATA: u8 = b'n';
pub const MSG_TYPE_PARSE_COMPLETE: u8 = b'1';
pub const MSG_TYPE_BIND_COMPLETE: u8 = b'2';
pub const MSG_TYPE_CLOSE_COMPLETE: u8 = b'3';
pub const MSG_TYPE_PORTAL_SUSPENDED: u8 = b's';

pub const MSG_TYPE_COPY_DATA: u8 = b'd';
pub const MSG_TYPE_COPY_DONE: u8 = b'c';
pub const MSG_TYPE_COPY_FAIL: u8 = b'f';
pub const MSG_TYPE_COPY_IN_RESPONSE: u8 = b'G';
pub const MSG_TYPE_COPY_OUT_RESPONSE: u8 = b'H';
pub const MSG_TYPE_COPY_BOTH_RESPONSE: u8 = b'W';

pub const MSG_TYPE_PASSWORD_MESSAGE: u8 = b'p';
pub const MSG_TYPE_TERMINATE: u8 = b'X';

pub const MSG_TYPE_SSL_REQUEST: u8 = 0;

pub const STARTUP_MESSAGE_MAGIC: u32 = 196608;

const INT32_SIZE: usize = 4;
const INT16_SIZE: usize = 2;

#[derive(Debug, Clone)]
pub struct PgMessage {
    pub msg_type: u8,
    pub payload: Bytes,
}

impl PgMessage {
    pub fn new(msg_type: u8, payload: Bytes) -> Self {
        Self { msg_type, payload }
    }

    pub fn encode(&self) -> Bytes {
        let mut buf = BytesMut::with_capacity(1 + INT32_SIZE + self.payload.len());
        buf.put_u8(self.msg_type);
        buf.put_i32((INT32_SIZE as i32) + self.payload.len() as i32);
        buf.put_slice(&self.payload);
        buf.freeze()
    }

    pub fn total_len(&self) -> usize {
        1 + INT32_SIZE + self.payload.len()
    }
}

#[derive(Debug, Clone)]
pub struct StartupMessage {
    pub protocol_version: i32,
    pub parameters: Vec<(String, String)>,
}

impl StartupMessage {
    pub fn from_payload(payload: &[u8]) -> io::Result<Self> {
        if payload.len() < INT32_SIZE {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Startup message too short",
            ));
        }

        let protocol_version = i32::from_be_bytes([
            payload[0], payload[1], payload[2], payload[3],
        ]);

        let mut parameters = Vec::new();
        let mut pos = INT32_SIZE;

        while pos < payload.len() {
            let name_start = pos;
            while pos < payload.len() && payload[pos] != 0 {
                pos += 1;
            }
            if pos >= payload.len() {
                break;
            }
            let name = String::from_utf8_lossy(&payload[name_start..pos]).to_string();
            pos += 1;

            if name.is_empty() {
                break;
            }

            let value_start = pos;
            while pos < payload.len() && payload[pos] != 0 {
                pos += 1;
            }
            if pos >= payload.len() {
                break;
            }
            let value = String::from_utf8_lossy(&payload[value_start..pos]).to_string();
            pos += 1;

            parameters.push((name, value));
        }

        Ok(Self {
            protocol_version,
            parameters,
        })
    }

    pub fn encode(&self) -> Bytes {
        let mut buf = BytesMut::new();
        buf.put_i32(self.protocol_version);
        for (name, value) in &self.parameters {
            buf.put_slice(name.as_bytes());
            buf.put_u8(0);
            buf.put_slice(value.as_bytes());
            buf.put_u8(0);
        }
        buf.put_u8(0);
        buf.freeze()
    }
}

#[derive(Debug, Clone)]
pub struct QueryMessage {
    pub query: String,
}

impl QueryMessage {
    pub fn from_payload(payload: &[u8]) -> io::Result<Self> {
        if payload.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Empty query message",
            ));
        }

        let query_bytes = &payload[..payload.len().saturating_sub(1)];
        let query = String::from_utf8_lossy(query_bytes).to_string();

        Ok(Self { query })
    }

    pub fn new(query: &str) -> Self {
        Self {
            query: query.to_string(),
        }
    }

    pub fn to_message(&self) -> PgMessage {
        let mut payload = BytesMut::new();
        payload.put_slice(self.query.as_bytes());
        payload.put_u8(0);
        PgMessage::new(MSG_TYPE_QUERY, payload.freeze())
    }
}

#[derive(Debug, Clone)]
pub struct ParseMessage {
    pub statement_name: String,
    pub query: String,
    pub param_data_types: Vec<i32>,
}

impl ParseMessage {
    pub fn from_payload(payload: &[u8]) -> io::Result<Self> {
        let mut pos = 0;

        let name_start = pos;
        while pos < payload.len() && payload[pos] != 0 {
            pos += 1;
        }
        let statement_name = String::from_utf8_lossy(&payload[name_start..pos]).to_string();
        pos += 1;

        let query_start = pos;
        while pos < payload.len() && payload[pos] != 0 {
            pos += 1;
        }
        let query = String::from_utf8_lossy(&payload[query_start..pos]).to_string();
        pos += 1;

        if pos + INT16_SIZE > payload.len() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Parse message too short",
            ));
        }
        let num_params = i16::from_be_bytes([payload[pos], payload[pos + 1]]) as usize;
        pos += INT16_SIZE;

        let mut param_data_types = Vec::with_capacity(num_params);
        for _ in 0..num_params {
            if pos + INT32_SIZE > payload.len() {
                break;
            }
            let oid = i32::from_be_bytes([
                payload[pos],
                payload[pos + 1],
                payload[pos + 2],
                payload[pos + 3],
            ]);
            param_data_types.push(oid);
            pos += INT32_SIZE;
        }

        Ok(Self {
            statement_name,
            query,
            param_data_types,
        })
    }

    pub fn encode(&self) -> Bytes {
        let mut buf = BytesMut::new();
        buf.put_slice(self.statement_name.as_bytes());
        buf.put_u8(0);
        buf.put_slice(self.query.as_bytes());
        buf.put_u8(0);
        buf.put_i16(self.param_data_types.len() as i16);
        for oid in &self.param_data_types {
            buf.put_i32(*oid);
        }
        buf.freeze()
    }
}

#[derive(Debug, Clone)]
pub struct BindMessage {
    pub portal_name: String,
    pub statement_name: String,
    pub param_format_codes: Vec<i16>,
    pub param_values: Vec<Option<Bytes>>,
    pub result_format_codes: Vec<i16>,
}

impl BindMessage {
    pub fn from_payload(payload: &[u8]) -> io::Result<Self> {
        let mut pos = 0;

        let portal_start = pos;
        while pos < payload.len() && payload[pos] != 0 {
            pos += 1;
        }
        let portal_name = String::from_utf8_lossy(&payload[portal_start..pos]).to_string();
        pos += 1;

        let stmt_start = pos;
        while pos < payload.len() && payload[pos] != 0 {
            pos += 1;
        }
        let statement_name = String::from_utf8_lossy(&payload[stmt_start..pos]).to_string();
        pos += 1;

        if pos + INT16_SIZE > payload.len() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Bind message too short",
            ));
        }
        let num_param_formats =
            i16::from_be_bytes([payload[pos], payload[pos + 1]]) as usize;
        pos += INT16_SIZE;

        let mut param_format_codes = Vec::with_capacity(num_param_formats);
        for _ in 0..num_param_formats {
            if pos + INT16_SIZE > payload.len() {
                break;
            }
            let code = i16::from_be_bytes([payload[pos], payload[pos + 1]]);
            param_format_codes.push(code);
            pos += INT16_SIZE;
        }

        if pos + INT16_SIZE > payload.len() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Bind message too short",
            ));
        }
        let num_params = i16::from_be_bytes([payload[pos], payload[pos + 1]]) as usize;
        pos += INT16_SIZE;

        let mut param_values = Vec::with_capacity(num_params);
        for _ in 0..num_params {
            if pos + INT32_SIZE > payload.len() {
                break;
            }
            let length =
                i32::from_be_bytes([
                    payload[pos],
                    payload[pos + 1],
                    payload[pos + 2],
                    payload[pos + 3],
                ]);
            pos += INT32_SIZE;

            if length < 0 {
                param_values.push(None);
            } else {
                let len = length as usize;
                if pos + len > payload.len() {
                    break;
                }
                let value = Bytes::copy_from_slice(&payload[pos..pos + len]);
                param_values.push(Some(value));
                pos += len;
            }
        }

        if pos + INT16_SIZE > payload.len() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Bind message too short",
            ));
        }
        let num_result_formats =
            i16::from_be_bytes([payload[pos], payload[pos + 1]]) as usize;
        pos += INT16_SIZE;

        let mut result_format_codes = Vec::with_capacity(num_result_formats);
        for _ in 0..num_result_formats {
            if pos + INT16_SIZE > payload.len() {
                break;
            }
            let code = i16::from_be_bytes([payload[pos], payload[pos + 1]]);
            result_format_codes.push(code);
            pos += INT16_SIZE;
        }

        Ok(Self {
            portal_name,
            statement_name,
            param_format_codes,
            param_values,
            result_format_codes,
        })
    }

    pub fn encode(&self) -> Bytes {
        let mut buf = BytesMut::new();
        buf.put_slice(self.portal_name.as_bytes());
        buf.put_u8(0);
        buf.put_slice(self.statement_name.as_bytes());
        buf.put_u8(0);
        buf.put_i16(self.param_format_codes.len() as i16);
        for code in &self.param_format_codes {
            buf.put_i16(*code);
        }
        buf.put_i16(self.param_values.len() as i16);
        for value in &self.param_values {
            match value {
                Some(v) => {
                    buf.put_i32(v.len() as i32);
                    buf.put_slice(v);
                }
                None => {
                    buf.put_i32(-1);
                }
            }
        }
        buf.put_i16(self.result_format_codes.len() as i16);
        for code in &self.result_format_codes {
            buf.put_i16(*code);
        }
        buf.freeze()
    }
}

#[derive(Debug, Clone)]
pub struct DataRowMessage {
    pub values: Vec<Option<Bytes>>,
}

impl DataRowMessage {
    pub fn from_payload(payload: &[u8]) -> io::Result<Self> {
        if payload.len() < INT16_SIZE {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Data row message too short",
            ));
        }

        let num_cols = i16::from_be_bytes([payload[0], payload[1]]) as usize;
        let mut pos = INT16_SIZE;

        let mut values = Vec::with_capacity(num_cols);
        for _ in 0..num_cols {
            if pos + INT32_SIZE > payload.len() {
                break;
            }
            let length = i32::from_be_bytes([
                payload[pos],
                payload[pos + 1],
                payload[pos + 2],
                payload[pos + 3],
            ]);
            pos += INT32_SIZE;

            if length < 0 {
                values.push(None);
            } else {
                let len = length as usize;
                if pos + len > payload.len() {
                    break;
                }
                let value = Bytes::copy_from_slice(&payload[pos..pos + len]);
                values.push(Some(value));
                pos += len;
            }
        }

        Ok(Self { values })
    }

    pub fn encode(&self) -> Bytes {
        let mut buf = BytesMut::new();
        buf.put_i16(self.values.len() as i16);
        for value in &self.values {
            match value {
                Some(v) => {
                    buf.put_i32(v.len() as i32);
                    buf.put_slice(v);
                }
                None => {
                    buf.put_i32(-1);
                }
            }
        }
        buf.freeze()
    }
}

#[derive(Debug, Clone)]
pub struct RowDescriptionMessage {
    pub fields: Vec<RowDescriptionField>,
}

#[derive(Debug, Clone)]
pub struct RowDescriptionField {
    pub name: String,
    pub table_oid: i32,
    pub column_index: i16,
    pub data_type_oid: i32,
    pub data_type_size: i16,
    pub type_modifier: i32,
    pub format_code: i16,
}

impl RowDescriptionMessage {
    pub fn from_payload(payload: &[u8]) -> io::Result<Self> {
        if payload.len() < INT16_SIZE {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Row description too short",
            ));
        }

        let num_fields = i16::from_be_bytes([payload[0], payload[1]]) as usize;
        let mut pos = INT16_SIZE;

        let mut fields = Vec::with_capacity(num_fields);
        for _ in 0..num_fields {
            let name_start = pos;
            while pos < payload.len() && payload[pos] != 0 {
                pos += 1;
            }
            let name = String::from_utf8_lossy(&payload[name_start..pos]).to_string();
            pos += 1;

            if pos + 18 > payload.len() {
                break;
            }

            let table_oid = i32::from_be_bytes([
                payload[pos],
                payload[pos + 1],
                payload[pos + 2],
                payload[pos + 3],
            ]);
            pos += INT32_SIZE;

            let column_index = i16::from_be_bytes([payload[pos], payload[pos + 1]]);
            pos += INT16_SIZE;

            let data_type_oid = i32::from_be_bytes([
                payload[pos],
                payload[pos + 1],
                payload[pos + 2],
                payload[pos + 3],
            ]);
            pos += INT32_SIZE;

            let data_type_size = i16::from_be_bytes([payload[pos], payload[pos + 1]]);
            pos += INT16_SIZE;

            let type_modifier = i32::from_be_bytes([
                payload[pos],
                payload[pos + 1],
                payload[pos + 2],
                payload[pos + 3],
            ]);
            pos += INT32_SIZE;

            let format_code = i16::from_be_bytes([payload[pos], payload[pos + 1]]);
            pos += INT16_SIZE;

            fields.push(RowDescriptionField {
                name,
                table_oid,
                column_index,
                data_type_oid,
                data_type_size,
                type_modifier,
                format_code,
            });
        }

        Ok(Self { fields })
    }
}

pub fn try_parse_message(buf: &mut BytesMut) -> io::Result<Option<PgMessage>> {
    if buf.len() < 1 + INT32_SIZE {
        return Ok(None);
    }

    let msg_type = buf[0];
    let length = i32::from_be_bytes([buf[1], buf[2], buf[3], buf[4]]) as usize;

    let total_len = 1 + length;
    if buf.len() < total_len {
        return Ok(None);
    }

    let payload = Bytes::copy_from_slice(&buf[1 + INT32_SIZE..total_len]);
    buf.advance(total_len);

    Ok(Some(PgMessage::new(msg_type, payload)))
}

pub fn try_parse_startup(buf: &mut BytesMut) -> io::Result<Option<(i32, Bytes)>> {
    if buf.len() < INT32_SIZE {
        return Ok(None);
    }

    let length = i32::from_be_bytes([buf[0], buf[1], buf[2], buf[3]]) as usize;
    if buf.len() < length {
        return Ok(None);
    }

    if length < INT32_SIZE + INT32_SIZE {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Startup message too short",
        ));
    }

    let protocol_version = i32::from_be_bytes([buf[4], buf[5], buf[6], buf[7]]);
    let payload = Bytes::copy_from_slice(&buf[INT32_SIZE..length]);
    buf.advance(length);

    Ok(Some((protocol_version, payload)))
}

pub fn is_ssl_request(buf: &[u8]) -> bool {
    if buf.len() < 8 {
        return false;
    }
    let length = i32::from_be_bytes([buf[0], buf[1], buf[2], buf[3]]) as usize;
    if length != 8 {
        return false;
    }
    let code = i32::from_be_bytes([buf[4], buf[5], buf[6], buf[7]]);
    code == 80877103
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_query_message_parse() {
        let query = "SELECT * FROM users";
        let msg = QueryMessage::new(query);
        let encoded = msg.to_message();
        let bytes = encoded.encode();

        let mut buf = BytesMut::from(&bytes[..]);
        let parsed = try_parse_message(&mut buf).unwrap().unwrap();

        assert_eq!(parsed.msg_type, MSG_TYPE_QUERY);
        let parsed_query = QueryMessage::from_payload(&parsed.payload).unwrap();
        assert_eq!(parsed_query.query, query);
    }

    #[test]
    fn test_data_row_parse() {
        let row = DataRowMessage {
            values: vec![
                Some(Bytes::from_static(b"hello")),
                None,
                Some(Bytes::from_static(b"world")),
            ],
        };

        let encoded = row.encode();
        let parsed = DataRowMessage::from_payload(&encoded).unwrap();
        assert_eq!(parsed.values.len(), 3);
        assert_eq!(parsed.values[0].as_ref().unwrap(), b"hello");
        assert!(parsed.values[1].is_none());
        assert_eq!(parsed.values[2].as_ref().unwrap(), b"world");
    }

    #[test]
    fn test_startup_message_parse() {
        let mut payload = BytesMut::new();
        payload.put_i32(STARTUP_MESSAGE_MAGIC);
        payload.put_slice(b"user\0postgres\0database\0testdb\0\0");

        let msg = StartupMessage::from_payload(&payload).unwrap();
        assert_eq!(msg.protocol_version, STARTUP_MESSAGE_MAGIC);
        assert_eq!(msg.parameters.len(), 2);
        assert_eq!(msg.parameters[0], ("user".to_string(), "postgres".to_string()));
        assert_eq!(msg.parameters[1], ("database".to_string(), "testdb".to_string()));
    }

    #[test]
    fn test_ssl_request_detection() {
        let mut buf = BytesMut::new();
        buf.put_i32(8);
        buf.put_i32(80877103);
        assert!(is_ssl_request(&buf));

        let mut buf2 = BytesMut::new();
        buf2.put_i32(8);
        buf2.put_i32(196608);
        assert!(!is_ssl_request(&buf2));
    }
}
