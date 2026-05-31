use lz4_flex::{compress_prepend_size, decompress_size_prepended};
use base64::{Engine as _, engine::general_purpose::STANDARD as BASE64};
use serde::Serialize;

pub const COMPRESSION_MAGIC: &[u8] = b"PG_LZ4_V1:";

pub const TOAST_THRESHOLD: usize = 2 * 1024;

pub const DEFAULT_MIN_COMPRESS_SIZE: usize = 4 * 1024;

pub const MAGIC_OVERHEAD: usize = 10;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CompressDecision {
    Compress,
    SkipTooSmall,
    SkipInefficient,
    SkipWouldTriggerToast,
}

#[derive(Debug, Clone)]
pub struct Compressor {
    pub compression_level: i32,
    pub min_compress_size: usize,
    pub toast_threshold: usize,
    pub adaptive_mode: bool,
}

impl Compressor {
    pub fn new(compression_level: i32, min_compress_size: usize) -> Self {
        Self {
            compression_level: compression_level.max(1).min(12),
            min_compress_size,
            toast_threshold: TOAST_THRESHOLD,
            adaptive_mode: true,
        }
    }

    pub fn with_toast_threshold(mut self, toast_threshold: usize) -> Self {
        self.toast_threshold = toast_threshold;
        self
    }

    pub fn set_adaptive_mode(&mut self, enabled: bool) {
        self.adaptive_mode = enabled;
    }

    pub fn should_compress(&self, data: &[u8]) -> bool {
        data.len() >= self.min_compress_size
    }

    pub fn decide_compression(&self, original: &[u8], compressed_size: usize) -> CompressDecision {
        let total_after = compressed_size + MAGIC_OVERHEAD;

        if original.len() < self.min_compress_size {
            return CompressDecision::SkipTooSmall;
        }

        if compressed_size >= original.len() {
            return CompressDecision::SkipInefficient;
        }

        if self.adaptive_mode && total_after > self.toast_threshold {
            return CompressDecision::SkipWouldTriggerToast;
        }

        CompressDecision::Compress
    }

    pub fn estimate_compressed_size(&self, data: &[u8]) -> usize {
        let compressed = compress_prepend_size(data);
        compressed.len()
    }

    pub fn compress(&self, data: &[u8]) -> Result<(Vec<u8>, CompressDecision), String> {
        if !self.should_compress(data) {
            return Ok((data.to_vec(), CompressDecision::SkipTooSmall));
        }

        let compressed = compress_prepend_size(data);

        let decision = self.decide_compression(data, compressed.len());

        match decision {
            CompressDecision::Compress => {
                let mut result = Vec::with_capacity(COMPRESSION_MAGIC.len() + compressed.len());
                result.extend_from_slice(COMPRESSION_MAGIC);
                result.extend_from_slice(&compressed);
                Ok((result, decision))
            }
            _ => Ok((data.to_vec(), decision)),
        }
    }

    pub fn decompress(data: &[u8]) -> Result<Vec<u8>, String> {
        if !data.starts_with(COMPRESSION_MAGIC) {
            return Ok(data.to_vec());
        }

        let compressed_data = &data[COMPRESSION_MAGIC.len()..];
        let decompressed = decompress_size_prepended(compressed_data)
            .map_err(|e| format!("LZ4解压失败: {}", e))?;
        Ok(decompressed)
    }

    pub fn is_compressed(data: &[u8]) -> bool {
        data.starts_with(COMPRESSION_MAGIC)
    }
}

#[derive(Debug, Clone, Default, Serialize)]
pub struct CompressionStats {
    pub original_bytes: u64,
    pub compressed_bytes: u64,
    pub decompressed_bytes: u64,
    pub compress_count: u64,
    pub decompress_count: u64,
    pub skipped_too_small: u64,
    pub skipped_inefficient: u64,
    pub skipped_toast: u64,
}

impl CompressionStats {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn record_compression(&mut self, original_size: u64, compressed_size: u64) {
        self.original_bytes += original_size;
        self.compressed_bytes += compressed_size;
        self.compress_count += 1;
    }

    pub fn record_decompression(&mut self, original_size: u64, decompressed_size: u64) {
        self.compressed_bytes += original_size;
        self.decompressed_bytes += decompressed_size;
        self.decompress_count += 1;
    }

    pub fn record_skipped(&mut self, size: u64, reason: CompressDecision) {
        self.original_bytes += size;
        self.compressed_bytes += size;
        match reason {
            CompressDecision::SkipTooSmall => self.skipped_too_small += 1,
            CompressDecision::SkipInefficient => self.skipped_inefficient += 1,
            CompressDecision::SkipWouldTriggerToast => self.skipped_toast += 1,
            CompressDecision::Compress => {}
        }
    }

    pub fn total_skipped(&self) -> u64 {
        self.skipped_too_small + self.skipped_inefficient + self.skipped_toast
    }

    pub fn compression_ratio(&self) -> f64 {
        if self.original_bytes == 0 {
            return 1.0;
        }
        self.compressed_bytes as f64 / self.original_bytes as f64
    }

    pub fn space_saved_percent(&self) -> f64 {
        if self.original_bytes == 0 {
            return 0.0;
        }
        (1.0 - self.compression_ratio()) * 100.0
    }

    pub fn format_size(bytes: u64) -> String {
        const UNITS: &[&str] = &["B", "KB", "MB", "GB", "TB"];
        let mut size = bytes as f64;
        let mut unit_idx = 0;
        while size >= 1024.0 && unit_idx < UNITS.len() - 1 {
            size /= 1024.0;
            unit_idx += 1;
        }
        format!("{:.2} {}", size, UNITS[unit_idx])
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_compress_decompress_roundtrip() {
        let compressor = Compressor::new(1, 10);
        let original = b"Hello, PostgreSQL Proxy with LZ4 compression! ".repeat(10);

        let (compressed, _) = compressor.compress(&original).unwrap();
        assert!(Compressor::is_compressed(&compressed));

        let decompressed = Compressor::decompress(&compressed).unwrap();
        assert_eq!(decompressed, original);
    }

    #[test]
    fn test_skip_small_data() {
        let compressor = Compressor::new(1, 100);
        let small_data = b"small";

        let (result, decision) = compressor.compress(small_data).unwrap();
        assert_eq!(result, small_data);
        assert!(!Compressor::is_compressed(&result));
        assert_eq!(decision, CompressDecision::SkipTooSmall);
    }

    #[test]
    fn test_decompress_non_compressed() {
        let data = b"normal data";
        let result = Compressor::decompress(data).unwrap();
        assert_eq!(result, data);
    }

    #[test]
    fn test_compression_stats() {
        let mut stats = CompressionStats::new();
        stats.record_compression(1000, 500);
        stats.record_compression(2000, 800);

        assert_eq!(stats.original_bytes, 3000);
        assert_eq!(stats.compressed_bytes, 1300);
        assert_eq!(stats.compress_count, 2);
        assert!((stats.compression_ratio() - 0.4333).abs() < 0.001);
        assert!((stats.space_saved_percent() - 56.67).abs() < 0.01);
    }

    #[test]
    fn test_adaptive_toast_avoidance() {
        let mut compressor = Compressor::new(1, 64);
        compressor.set_adaptive_mode(true);

        let large_data = vec![b'A'; 5000];
        let (result, decision) = compressor.compress(&large_data).unwrap();

        match decision {
            CompressDecision::Compress => {
                assert!(Compressor::is_compressed(&result));
            }
            CompressDecision::SkipWouldTriggerToast => {
                assert!(!Compressor::is_compressed(&result));
                assert_eq!(result, large_data);
            }
            _ => {}
        }
    }

    #[test]
    fn test_compress_decision() {
        let compressor = Compressor::new(1, 100);

        let small_data = vec![b'x'; 50];
        assert_eq!(
            compressor.decide_compression(&small_data, 20),
            CompressDecision::SkipTooSmall
        );

        let med_data = vec![b'y'; 200];
        assert_eq!(
            compressor.decide_compression(&med_data, 200),
            CompressDecision::SkipInefficient
        );
    }

    #[test]
    fn test_toast_threshold_constant() {
        assert_eq!(TOAST_THRESHOLD, 2048);
        assert_eq!(DEFAULT_MIN_COMPRESS_SIZE, 4096);
        assert_eq!(MAGIC_OVERHEAD, 10);
    }
}
