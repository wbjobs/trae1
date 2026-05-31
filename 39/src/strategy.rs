use std::sync::Arc;

use parking_lot::RwLock;
use serde::{Deserialize, Serialize};

use crate::compression::Algo;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LevelThresholds {
    pub small_file_max_bytes: u64,
    pub medium_file_max_bytes: u64,
    pub gzip_small: u32,
    pub gzip_medium: u32,
    pub gzip_large: u32,
    pub brotli_small: u32,
    pub brotli_medium: u32,
    pub brotli_large: u32,
    pub deflate_small: u32,
    pub deflate_medium: u32,
    pub deflate_large: u32,
}

impl Default for LevelThresholds {
    fn default() -> Self {
        Self {
            small_file_max_bytes: 64 * 1024,
            medium_file_max_bytes: 512 * 1024,
            gzip_small: 9,
            gzip_medium: 6,
            gzip_large: 4,
            brotli_small: 11,
            brotli_medium: 6,
            brotli_large: 4,
            deflate_small: 9,
            deflate_medium: 6,
            deflate_large: 4,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum SizeTier {
    Small,
    Medium,
    Large,
}

impl SizeTier {
    pub fn as_str(&self) -> &'static str {
        match self {
            SizeTier::Small => "small",
            SizeTier::Medium => "medium",
            SizeTier::Large => "large",
        }
    }
}

#[derive(Debug, Clone)]
pub struct CompressionLevels {
    pub gzip: u32,
    pub brotli: u32,
    pub deflate: u32,
    pub tier: SizeTier,
}

#[derive(Clone)]
pub struct Strategy {
    inner: Arc<RwLock<LevelThresholds>>,
}

impl Strategy {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(RwLock::new(LevelThresholds::default())),
        }
    }

    pub fn from_thresholds(t: LevelThresholds) -> Self {
        Self {
            inner: Arc::new(RwLock::new(t)),
        }
    }

    pub fn thresholds(&self) -> LevelThresholds {
        self.inner.read().clone()
    }

    pub fn update(&self, t: LevelThresholds) {
        *self.inner.write() = t;
    }

    pub fn classify(&self, size: u64) -> SizeTier {
        let t = self.inner.read();
        if size <= t.small_file_max_bytes {
            SizeTier::Small
        } else if size <= t.medium_file_max_bytes {
            SizeTier::Medium
        } else {
            SizeTier::Large
        }
    }

    pub fn levels_for_size(&self, size: u64) -> CompressionLevels {
        let t = self.inner.read();
        let tier = if size <= t.small_file_max_bytes {
            SizeTier::Small
        } else if size <= t.medium_file_max_bytes {
            SizeTier::Medium
        } else {
            SizeTier::Large
        };
        let (gzip, brotli, deflate) = match tier {
            SizeTier::Small => (t.gzip_small, t.brotli_small, t.deflate_small),
            SizeTier::Medium => (t.gzip_medium, t.brotli_medium, t.deflate_medium),
            SizeTier::Large => (t.gzip_large, t.brotli_large, t.deflate_large),
        };
        CompressionLevels {
            gzip,
            brotli,
            deflate,
            tier,
        }
    }

    pub fn level_for(&self, algo: Algo, size: u64) -> u32 {
        let lv = self.levels_for_size(size);
        match algo {
            Algo::Gzip => lv.gzip,
            Algo::Brotli => lv.brotli,
            Algo::Deflate => lv.deflate,
            Algo::Identity => 0,
        }
    }
}

impl Default for Strategy {
    fn default() -> Self {
        Self::new()
    }
}
