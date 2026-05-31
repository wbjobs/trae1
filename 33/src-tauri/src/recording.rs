use std::fs;
use std::path::PathBuf;

use serde::{Deserialize, Serialize};
use tauri::Manager;

use crate::error::AppError;

#[derive(Debug, Serialize)]
pub struct Recording {
    pub filename: String,
    pub path: String,
    pub size_bytes: u64,
    pub created_at: String,
}

#[derive(Debug, Deserialize)]
pub struct SaveRecordingArgs {
    pub filename: String,
    pub data_base64: String,
}

fn recordings_dir(app: &tauri::AppHandle) -> Result<PathBuf, AppError> {
    let dir = app
        .path()
        .resolve("recordings", tauri::path::BaseDirectory::AppData)
        .map_err(|e| AppError::Io(e.to_string()))?;
    fs::create_dir_all(&dir)?;
    Ok(dir)
}

#[tauri::command]
pub async fn save_recording(
    app: tauri::AppHandle,
    args: SaveRecordingArgs,
) -> Result<String, AppError> {
    let dir = recordings_dir(&app)?;
    let filename = if args.filename.to_ascii_lowercase().ends_with(".mp4")
        || args.filename.to_ascii_lowercase().ends_with(".webm")
    {
        args.filename
    } else {
        format!("{}.mp4", args.filename)
    };

    let path = dir.join(&filename);
    let bytes = base64_decode(&args.data_base64)
        .map_err(|e| AppError::InvalidFrame(format!("base64 decode failed: {e}")))?;
    fs::write(&path, bytes)?;
    Ok(path.to_string_lossy().into_owned())
}

#[tauri::command]
pub async fn list_recordings(app: tauri::AppHandle) -> Result<Vec<Recording>, AppError> {
    let dir = recordings_dir(&app)?;
    let mut out = Vec::new();
    for entry in fs::read_dir(&dir)? {
        let entry = entry?;
        let path = entry.path();
        if !path.is_file() {
            continue;
        }
        let meta = fs::metadata(&path)?;
        let modified = meta
            .modified()
            .ok()
            .and_then(|t| time::OffsetDateTime::try_from(t).ok())
            .map(|t| t.format(&time::format_description::well_known::Rfc3339).unwrap_or_default())
            .unwrap_or_default();
        out.push(Recording {
            filename: path
                .file_name()
                .map(|f| f.to_string_lossy().into_owned())
                .unwrap_or_default(),
            path: path.to_string_lossy().into_owned(),
            size_bytes: meta.len(),
            created_at: modified,
        });
    }
    out.sort_by(|a, b| b.created_at.cmp(&a.created_at));
    Ok(out)
}

fn base64_decode(s: &str) -> Result<Vec<u8>, String> {
    use std::io::Read;

    let cleaned: String = s.chars().filter(|c| !c.is_whitespace()).collect();
    if cleaned.is_empty() {
        return Ok(Vec::new());
    }

    const ALPHABET: &[u8; 64] =
        b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    let bytes = cleaned.as_bytes();
    let mut out = Vec::with_capacity((bytes.len() / 4) * 3);
    let mut i = 0;
    while i + 4 <= bytes.len() {
        let c = |b: u8| -> u32 {
            if b == b'=' {
                0
            } else {
                ALPHABET.iter().position(|&x| x == b).map(|x| x as u32).unwrap_or(0)
            }
        };
        let a = c(bytes[i]);
        let b = c(bytes[i + 1]);
        let c1 = c(bytes[i + 2]);
        let d = c(bytes[i + 3]);
        let triple = (a << 18) | (b << 12) | (c1 << 6) | d;
        out.push(((triple >> 16) & 0xFF) as u8);
        if bytes[i + 2] != b'=' {
            out.push(((triple >> 8) & 0xFF) as u8);
        }
        if bytes[i + 3] != b'=' {
            out.push((triple & 0xFF) as u8);
        }
        i += 4;
    }
    // suppress unused
    let _ = Read::read;
    Ok(out)
}
