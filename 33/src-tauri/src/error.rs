use std::fmt;

use serde::Serialize;

#[derive(Debug, thiserror::Error)]
pub enum AppError {
    #[error("ONNX Runtime error: {0}")]
    Ort(String),
    #[error("Image processing error: {0}")]
    Image(String),
    #[error("Model not loaded")]
    ModelNotLoaded,
    #[error("Invalid frame data: {0}")]
    InvalidFrame(String),
    #[error("I/O error: {0}")]
    Io(String),
    #[error("{0}")]
    Other(String),
}

impl Serialize for AppError {
    fn serialize<S: serde::Serializer>(&self, s: S) -> Result<S::Ok, S::Error> {
        s.serialize_str(&self.to_string())
    }
}

impl fmt::Display for AppError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AppError::Ort(e) => write!(f, "ONNX Runtime error: {e}"),
            AppError::Image(e) => write!(f, "Image processing error: {e}"),
            AppError::ModelNotLoaded => write!(f, "Model not loaded"),
            AppError::InvalidFrame(e) => write!(f, "Invalid frame data: {e}"),
            AppError::Io(e) => write!(f, "I/O error: {e}"),
            AppError::Other(e) => write!(f, "{e}"),
        }
    }
}

impl From<ort::Error> for AppError {
    fn from(e: ort::Error) -> Self {
        AppError::Ort(e.to_string())
    }
}

impl From<image::ImageError> for AppError {
    fn from(e: image::ImageError) -> Self {
        AppError::Image(e.to_string())
    }
}

impl From<std::io::Error> for AppError {
    fn from(e: std::io::Error) -> Self {
        AppError::Io(e.to_string())
    }
}

impl From<anyhow::Error> for AppError {
    fn from(e: anyhow::Error) -> Self {
        AppError::Other(e.to_string())
    }
}
