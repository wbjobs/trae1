use crate::point_cloud::{Color, Point, PointCloud};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CompressedPointCloud {
    pub header: CompressionHeader,
    pub data: Vec<u8>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CompressionHeader {
    pub num_points: usize,
    pub bounds_min: [f32; 3],
    pub bounds_max: [f32; 3],
    pub quantization_bits: u8,
    pub has_colors: bool,
    pub has_normals: bool,
    pub original_size: usize,
    pub compressed_size: usize,
}

const DEFAULT_QUANTIZATION_BITS: u8 = 14;
const MAX_QUANT_VALUE: u32 = (1u32 << DEFAULT_QUANTIZATION_BITS) - 1;

pub fn compress_point_cloud(cloud: &PointCloud) -> Result<CompressedPointCloud, CompressionError> {
    let has_colors = !cloud.colors.is_empty();
    let has_normals = cloud.normals.is_some();

    let mut data = Vec::new();

    let min = cloud.bounds.min.to_array();
    let max = cloud.bounds.max.to_array();

    let range = [
        (max[0] - min[0]).max(1e-6),
        (max[1] - min[1]).max(1e-6),
        (max[2] - min[2]).max(1e-6),
    ];

    let original_size = cloud.points.len() * (3 * 4 + if has_colors { 4 } else { 0 });

    for (i, point) in cloud.points.iter().enumerate() {
        let qx = quantize(point.x, min[0], range[0]);
        let qy = quantize(point.y, min[1], range[1]);
        let qz = quantize(point.z, min[2], range[2]);

        data.extend_from_slice(&qx.to_le_bytes());
        data.extend_from_slice(&qy.to_le_bytes());
        data.extend_from_slice(&qz.to_le_bytes());

        if has_colors {
            if let Some(color) = cloud.colors.get(i) {
                data.push(color.r);
                data.push(color.g);
                data.push(color.b);
                data.push(color.a);
            } else {
                data.extend_from_slice(&[255u8, 255, 255, 255]);
            }
        }
    }

    let compressed = simple_compress(&data);

    Ok(CompressedPointCloud {
        header: CompressionHeader {
            num_points: cloud.points.len(),
            bounds_min: min,
            bounds_max: max,
            quantization_bits: DEFAULT_QUANTIZATION_BITS,
            has_colors,
            has_normals,
            original_size,
            compressed_size: compressed.len(),
        },
        data: compressed,
    })
}

pub fn decompress_point_cloud(compressed: &CompressedPointCloud) -> Result<PointCloud, CompressionError> {
    let data = simple_decompress(&compressed.data)?;

    let header = &compressed.header;
    let range = [
        (header.bounds_max[0] - header.bounds_min[0]).max(1e-6),
        (header.bounds_max[1] - header.bounds_min[1]).max(1e-6),
        (header.bounds_max[2] - header.bounds_min[2]).max(1e-6),
    ];

    let bytes_per_point = if header.has_colors { 10 } else { 6 };
    let expected_size = header.num_points * bytes_per_point;

    if data.len() < expected_size {
        return Err(CompressionError::DecompressionError(format!(
            "Data size mismatch: expected {}, got {}",
            expected_size,
            data.len()
        )));
    }

    let mut points = Vec::with_capacity(header.num_points);
    let mut colors = if header.has_colors {
        Vec::with_capacity(header.num_points)
    } else {
        Vec::new()
    };

    for i in 0..header.num_points {
        let offset = i * bytes_per_point;

        let qx = u32::from_le_bytes([
            data[offset],
            data[offset + 1],
            data[offset + 2],
            data[offset + 3],
        ]);
        let qy = u32::from_le_bytes([
            data[offset + 4],
            data[offset + 5],
            data[offset + 6],
            data[offset + 7],
        ]);
        let qz = if header.has_colors {
            0
        } else {
            u32::from_le_bytes([
                data[offset + 8],
                data[offset + 9],
                data[offset + 10],
                data[offset + 11],
            ])
        };

        let (x, y, z) = if header.has_colors {
            let qz_val = u32::from_le_bytes([
                data[offset + 8],
                data[offset + 9],
                0,
                0,
            ]);
            (
                dequantize(qx, header.bounds_min[0], range[0]),
                dequantize(qy, header.bounds_min[1], range[1]),
                dequantize(qz_val, header.bounds_min[2], range[2]),
            )
        } else {
            (
                dequantize(qx, header.bounds_min[0], range[0]),
                dequantize(qy, header.bounds_min[1], range[1]),
                dequantize(qz, header.bounds_min[2], range[2]),
            )
        };

        points.push(Point::new(x, y, z));

        if header.has_colors {
            let color_offset = offset + 6;
            if color_offset + 3 < data.len() {
                colors.push(Color::new(
                    data[color_offset],
                    data[color_offset + 1],
                    data[color_offset + 2],
                ));
            } else {
                colors.push(Color::new(255, 255, 255));
            }
        }
    }

    Ok(PointCloud::new(points, colors, None))
}

fn quantize(value: f32, min: f32, range: f32) -> u32 {
    let normalized = ((value - min) / range).clamp(0.0, 1.0);
    (normalized * MAX_QUANT_VALUE as f32) as u32
}

fn dequantize(quantized: u32, min: f32, range: f32) -> f32 {
    let normalized = quantized as f32 / MAX_QUANT_VALUE as f32;
    min + normalized * range
}

fn simple_compress(data: &[u8]) -> Vec<u8> {
    if data.is_empty() {
        return Vec::new();
    }

    let mut compressed = Vec::with_capacity(data.len());
    let mut i = 0;

    while i < data.len() {
        let byte = data[i];
        let mut count: u8 = 1;

        while i + (count as usize) < data.len()
            && data[i + count as usize] == byte
            && count < 255
        {
            count += 1;
        }

        if count > 1 || byte == 0 {
            compressed.push(count);
            compressed.push(byte);
        } else {
            compressed.push(1);
            compressed.push(byte);
        }

        i += count as usize;
    }

    compressed
}

fn simple_decompress(data: &[u8]) -> Result<Vec<u8>, CompressionError> {
    if data.is_empty() {
        return Ok(Vec::new());
    }

    if data.len() % 2 != 0 {
        return Err(CompressionError::DecompressionError(
            "Invalid compressed data length".to_string(),
        ));
    }

    let mut decompressed = Vec::new();
    let mut i = 0;

    while i < data.len() {
        let count = data[i] as usize;
        let byte = data[i + 1];

        for _ in 0..count {
            decompressed.push(byte);
        }

        i += 2;
    }

    Ok(decompressed)
}

pub fn serialize_compressed(compressed: &CompressedPointCloud) -> Result<Vec<u8>, CompressionError> {
    let header_json = serde_json::to_vec(&compressed.header)
        .map_err(|e| CompressionError::SerializationError(e.to_string()))?;

    let header_len = (header_json.len() as u32).to_le_bytes();

    let mut result = Vec::new();
    result.extend_from_slice(&header_len);
    result.extend_from_slice(&header_json);
    result.extend_from_slice(&compressed.data);

    Ok(result)
}

pub fn deserialize_compressed(data: &[u8]) -> Result<CompressedPointCloud, CompressionError> {
    if data.len() < 4 {
        return Err(CompressionError::DeserializationError(
            "Data too short for header length".to_string(),
        ));
    }

    let header_len = u32::from_le_bytes([data[0], data[1], data[2], data[3]]) as usize;

    if data.len() < 4 + header_len {
        return Err(CompressionError::DeserializationError(
            "Data too short for header".to_string(),
        ));
    }

    let header: CompressionHeader =
        serde_json::from_slice(&data[4..4 + header_len])
            .map_err(|e| CompressionError::DeserializationError(e.to_string()))?;

    let compressed_data = data[4 + header_len..].to_vec();

    Ok(CompressedPointCloud {
        header,
        data: compressed_data,
    })
}

#[derive(Debug, thiserror::Error)]
pub enum CompressionError {
    #[error("Compression error: {0}")]
    CompressionError(String),
    #[error("Decompression error: {0}")]
    DecompressionError(String),
    #[error("Serialization error: {0}")]
    SerializationError(String),
    #[error("Deserialization error: {0}")]
    DeserializationError(String),
}
