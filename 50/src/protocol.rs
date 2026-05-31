use bytes::{Buf, BufMut, Bytes, BytesMut};
use serde::{Deserialize, Serialize};

pub const CHUNK_SIZE: usize = 1024 * 1024;
pub const MAX_FILES: usize = 100;
pub const MAX_FILE_SIZE: u64 = 100 * 1024 * 1024 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum MessageType {
    FileMeta = 1,
    FileData = 2,
    FileDone = 3,
    ResumeRequest = 4,
    ResumeAck = 5,
    TransferComplete = 6,
    Error = 7,
    Ack = 8,
}

impl MessageType {
    pub fn from_u8(val: u8) -> Option<Self> {
        match val {
            1 => Some(MessageType::FileMeta),
            2 => Some(MessageType::FileData),
            3 => Some(MessageType::FileDone),
            4 => Some(MessageType::ResumeRequest),
            5 => Some(MessageType::ResumeAck),
            6 => Some(MessageType::TransferComplete),
            7 => Some(MessageType::Error),
            8 => Some(MessageType::Ack),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FileMeta {
    pub file_id: u32,
    pub file_name: String,
    pub file_size: u64,
    pub is_tar: bool,
    pub num_files: Option<u32>,
    pub sha256: Option<String>,
}

#[derive(Debug, Clone)]
pub struct FileData {
    pub file_id: u32,
    pub offset: u64,
    pub data: Bytes,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FileDone {
    pub file_id: u32,
    pub sha256: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ResumeRequest {
    pub file_id: u32,
    pub offset: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ResumeAck {
    pub file_id: u32,
    pub offset: u64,
    pub accepted: bool,
}

pub fn encode_meta(meta: &FileMeta) -> anyhow::Result<Bytes> {
    let json = serde_json::to_vec(meta)?;
    let mut buf = BytesMut::with_capacity(5 + json.len());
    buf.put_u8(MessageType::FileMeta as u8);
    buf.put_u32(json.len() as u32);
    buf.put_slice(&json);
    Ok(buf.freeze())
}

pub fn encode_data(data: &FileData) -> Bytes {
    let mut buf = BytesMut::with_capacity(1 + 4 + 4 + 8 + data.data.len());
    buf.put_u8(MessageType::FileData as u8);
    let payload_len = 4 + 8 + data.data.len();
    buf.put_u32(payload_len as u32);
    buf.put_u32(data.file_id);
    buf.put_u64(data.offset);
    buf.put_slice(&data.data);
    buf.freeze()
}

pub fn encode_done(done: &FileDone) -> anyhow::Result<Bytes> {
    let json = serde_json::to_vec(done)?;
    let mut buf = BytesMut::with_capacity(5 + json.len());
    buf.put_u8(MessageType::FileDone as u8);
    buf.put_u32(json.len() as u32);
    buf.put_slice(&json);
    Ok(buf.freeze())
}

pub fn encode_resume_request(req: &ResumeRequest) -> anyhow::Result<Bytes> {
    let json = serde_json::to_vec(req)?;
    let mut buf = BytesMut::with_capacity(5 + json.len());
    buf.put_u8(MessageType::ResumeRequest as u8);
    buf.put_u32(json.len() as u32);
    buf.put_slice(&json);
    Ok(buf.freeze())
}

pub fn encode_resume_ack(ack: &ResumeAck) -> anyhow::Result<Bytes> {
    let json = serde_json::to_vec(ack)?;
    let mut buf = BytesMut::with_capacity(5 + json.len());
    buf.put_u8(MessageType::ResumeAck as u8);
    buf.put_u32(json.len() as u32);
    buf.put_slice(&json);
    Ok(buf.freeze())
}

pub fn encode_transfer_complete() -> Bytes {
    let mut buf = BytesMut::with_capacity(5);
    buf.put_u8(MessageType::TransferComplete as u8);
    buf.put_u32(0);
    buf.freeze()
}

pub fn encode_ack(file_id: u32, offset: u64) -> Bytes {
    let mut buf = BytesMut::with_capacity(1 + 4 + 12);
    buf.put_u8(MessageType::Ack as u8);
    buf.put_u32(12);
    buf.put_u32(file_id);
    buf.put_u64(offset);
    buf.freeze()
}

pub fn encode_error(msg: &str) -> Bytes {
    let msg_bytes = msg.as_bytes();
    let mut buf = BytesMut::with_capacity(5 + msg_bytes.len());
    buf.put_u8(MessageType::Error as u8);
    buf.put_u32(msg_bytes.len() as u32);
    buf.put_slice(msg_bytes);
    buf.freeze()
}

#[derive(Debug)]
#[allow(dead_code)]
pub enum Message {
    FileMeta(FileMeta),
    FileData(FileData),
    FileDone(FileDone),
    ResumeRequest(ResumeRequest),
    ResumeAck(ResumeAck),
    TransferComplete,
    Ack { file_id: u32, offset: u64 },
    Error(String),
}

pub async fn read_message(
    stream: &mut quinn::RecvStream,
) -> anyhow::Result<Message> {
    let mut header = vec![0u8; 5];
    stream.read_exact(&mut header).await?;
    let msg_type = MessageType::from_u8(header[0])
        .ok_or_else(|| anyhow::anyhow!("Unknown message type: {}", header[0]))?;
    let payload_len = (&header[1..5]).get_u32() as usize;

    let mut payload = vec![0u8; payload_len];
    if payload_len > 0 {
        stream.read_exact(&mut payload).await?;
    }

    match msg_type {
        MessageType::FileMeta => {
            let meta: FileMeta = serde_json::from_slice(&payload)?;
            Ok(Message::FileMeta(meta))
        }
        MessageType::FileData => {
            if payload.len() < 12 {
                anyhow::bail!("FileData payload too short");
            }
            let mut cursor = &payload[..];
            let file_id = cursor.get_u32();
            let offset = cursor.get_u64();
            let data = Bytes::copy_from_slice(cursor);
            Ok(Message::FileData(FileData {
                file_id,
                offset,
                data,
            }))
        }
        MessageType::FileDone => {
            let done: FileDone = serde_json::from_slice(&payload)?;
            Ok(Message::FileDone(done))
        }
        MessageType::ResumeRequest => {
            let req: ResumeRequest = serde_json::from_slice(&payload)?;
            Ok(Message::ResumeRequest(req))
        }
        MessageType::ResumeAck => {
            let ack: ResumeAck = serde_json::from_slice(&payload)?;
            Ok(Message::ResumeAck(ack))
        }
        MessageType::TransferComplete => Ok(Message::TransferComplete),
        MessageType::Ack => {
            if payload.len() < 12 {
                anyhow::bail!("Ack payload too short");
            }
            let mut cursor = &payload[..];
            let file_id = cursor.get_u32();
            let offset = cursor.get_u64();
            Ok(Message::Ack { file_id, offset })
        }
        MessageType::Error => {
            let msg = String::from_utf8_lossy(&payload).to_string();
            Ok(Message::Error(msg))
        }
    }
}
