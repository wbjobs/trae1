use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;

use arrow::array::RecordBatch;
use arrow::datatypes::Schema;
use arrow::error::ArrowError;
use arrow::ipc::reader::FileReader;
use parking_lot::RwLock;
use serde::{Deserialize, Serialize};
use tracing::info;

use crate::proto::columnar::gateway::DatasetInfo;

pub type CompressionCodecRef = Option<arrow_ipc::CompressionCodec>;

#[derive(Debug, thiserror::Error)]
pub enum StorageError {
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Arrow error: {0}")]
    Arrow(#[from] ArrowError),
    #[error("JSON error: {0}")]
    Json(#[from] serde_json::Error),
    #[error("Dataset not found: {0}")]
    NotFound(String),
    #[error("Version not found: {0} v{1}")]
    VersionNotFound(String, i64),
    #[error("Invalid dataset name: {0}")]
    InvalidName(String),
}

pub type Result<T> = std::result::Result<T, StorageError>;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VersionMeta {
    pub version: i64,
    pub created_at: i64,
    pub row_count: i64,
    pub bytes: i64,
    pub schema_json: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct DatasetMeta {
    pub name: String,
    pub versions: BTreeMap<i64, VersionMeta>,
}

impl DatasetMeta {
    pub fn latest_version(&self) -> Option<i64> {
        self.versions.keys().next_back().copied()
    }
    pub fn next_version(&self) -> i64 {
        self.latest_version().map(|v| v + 1).unwrap_or(1)
    }
}

fn validate_name(name: &str) -> Result<()> {
    if name.is_empty()
        || name.len() > 128
        || !name
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-' || c == '.')
    {
        return Err(StorageError::InvalidName(name.to_string()));
    }
    Ok(())
}

fn version_info(name: &str, meta: &VersionMeta) -> DatasetInfo {
    DatasetInfo {
        name: name.to_string(),
        version: meta.version,
        created_at: meta.created_at,
        row_count: meta.row_count,
        bytes: meta.bytes,
        schema_json: meta.schema_json.clone(),
    }
}

/// 列式数据存储（磁盘：{data_dir}/{dataset}/v{version}.arrow + meta.json）
pub struct ArrowStorage {
    root: PathBuf,
    max_versions: usize,
    #[allow(dead_code)]
    batch_rows: usize,
    metas: Arc<RwLock<BTreeMap<String, DatasetMeta>>>,
    compression: CompressionCodecRef,
}

impl ArrowStorage {
    pub async fn new(
        data_dir: impl AsRef<Path>,
        max_versions: usize,
        batch_rows: usize,
        compression: CompressionCodecRef,
    ) -> Result<Self> {
        let root = PathBuf::from(data_dir.as_ref());
        tokio::fs::create_dir_all(&root).await?;

        let mut metas = BTreeMap::new();
        let mut rd = tokio::fs::read_dir(&root).await?;
        while let Some(entry) = rd.next_entry().await? {
            let name = entry.file_name().to_string_lossy().to_string();
            let meta_path = entry.path().join("meta.json");
            if meta_path.exists() {
                let bytes = tokio::fs::read(&meta_path).await?;
                match serde_json::from_slice::<DatasetMeta>(&bytes) {
                    Ok(mut m) => {
                        m.name = name.clone();
                        metas.insert(name, m);
                    }
                    Err(e) => {
                        tracing::warn!("skip corrupt meta {meta_path:?}: {e}");
                    }
                }
            }
        }

        Ok(Self {
            root,
            max_versions,
            batch_rows,
            metas: Arc::new(RwLock::new(metas)),
            compression,
        })
    }

    pub fn compression(&self) -> &CompressionCodecRef {
        &self.compression
    }

    pub fn batch_rows(&self) -> usize {
        self.batch_rows
    }

    fn dataset_dir(&self, name: &str) -> PathBuf {
        self.root.join(name)
    }

    fn version_file(&self, name: &str, version: i64) -> PathBuf {
        self.root.join(name).join(format!("v{}.arrow", version))
    }

    fn meta_file(&self, name: &str) -> PathBuf {
        self.root.join(name).join("meta.json")
    }

    pub fn list_datasets(&self) -> Vec<String> {
        self.metas.read().keys().cloned().collect()
    }

    pub fn list_versions(&self, name: &str) -> Result<Vec<DatasetInfo>> {
        validate_name(name)?;
        let map = self.metas.read();
        let meta = map
            .get(name)
            .ok_or_else(|| StorageError::NotFound(name.to_string()))?;
        let mut out: Vec<DatasetInfo> = meta
            .versions
            .values()
            .map(|v| version_info(name, v))
            .collect();
        out.sort_by_key(|v| std::cmp::Reverse(v.version));
        Ok(out)
    }

    pub fn get_version(&self, name: &str, version: i64) -> Result<(VersionMeta, Schema)> {
        validate_name(name)?;
        let map = self.metas.read();
        let meta = map
            .get(name)
            .ok_or_else(|| StorageError::NotFound(name.to_string()))?;
        let vm = if version == 0 {
            meta.latest_version()
                .and_then(|v| meta.versions.get(&v).cloned())
                .ok_or_else(|| StorageError::VersionNotFound(name.to_string(), 0))?
        } else {
            meta.versions
                .get(&version)
                .cloned()
                .ok_or_else(|| StorageError::VersionNotFound(name.to_string(), version))?
        };
        let schema: Schema = serde_json::from_str(&vm.schema_json)?;
        Ok((vm, schema))
    }

    pub async fn delete(&self, name: &str, version: i64) -> Result<bool> {
        validate_name(name)?;
        if version == 0 {
            let dir = self.dataset_dir(name);
            self.metas.write().remove(name);
            let _ = tokio::fs::remove_dir_all(dir).await;
            return Ok(true);
        }
        let mut map = self.metas.write();
        let meta = map
            .get_mut(name)
            .ok_or_else(|| StorageError::NotFound(name.to_string()))?;
        if meta.versions.remove(&version).is_some() {
            let file = self.version_file(name, version);
            let _ = tokio::fs::remove_file(file).await;
            self.persist_meta_locked(name, meta).await?;
            Ok(true)
        } else {
            Err(StorageError::VersionNotFound(name.to_string(), version))
        }
    }

    async fn persist_meta_locked(&self, name: &str, meta: &DatasetMeta) -> Result<()> {
        let path = self.meta_file(name);
        let json = serde_json::to_vec_pretty(meta)?;
        tokio::fs::write(&path, json).await?;
        Ok(())
    }

    /// 在锁内分配新版本号并返回 final/tmp 路径。
    /// 不提交到 meta.json。
    pub fn reserve_version(&self, name: &str) -> Result<(i64, PathBuf, PathBuf)> {
        validate_name(name)?;
        std::fs::create_dir_all(self.dataset_dir(name))?;
        let mut map = self.metas.write();
        let meta = map.entry(name.to_string()).or_default();
        meta.name = name.to_string();
        let v = meta.next_version();
        let final_path = self.version_file(name, v);
        let tmp_path = self.dataset_dir(name).join(format!("v{}.arrow.tmp", v));
        Ok((v, tmp_path, final_path))
    }

    /// 提交已经写好的 tmp 文件为新版本。调用者需保证 tmp_path 和 final_path 对应 reserve_version。
    pub async fn commit_version(
        &self,
        name: &str,
        version: i64,
        tmp_path: PathBuf,
        final_path: PathBuf,
        row_count: i64,
        bytes: i64,
        schema_json: String,
    ) -> Result<DatasetInfo> {
        tokio::fs::rename(&tmp_path, &final_path).await?;
        let mut to_delete: Vec<i64> = Vec::new();
        let info;
        {
            let mut map = self.metas.write();
            let meta = map.entry(name.to_string()).or_default();
            meta.name = name.to_string();
            let vm = VersionMeta {
                version,
                created_at: std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map(|d| d.as_secs() as i64)
                    .unwrap_or(0),
                row_count,
                bytes,
                schema_json,
            };
            info = version_info(name, &vm);
            meta.versions.insert(version, vm);
            while meta.versions.len() > self.max_versions {
                let oldest = *meta.versions.keys().next().unwrap();
                meta.versions.remove(&oldest);
                to_delete.push(oldest);
            }
            self.persist_meta_locked(name, meta).await?;
        }
        for v in to_delete {
            let p = self.version_file(name, v);
            let _ = tokio::fs::remove_file(p).await;
            info!("pruned old version {name} v{v}");
        }
        Ok(info)
    }

    pub async fn read_record_batches(
        &self,
        name: &str,
        version: i64,
    ) -> Result<Vec<RecordBatch>> {
        let (vm, _schema) = self.get_version(name, version)?;
        let version = if version == 0 { vm.version } else { version };
        let path = self.version_file(name, version);
        let data = tokio::fs::read(&path).await?;
        let reader = FileReader::try_new(std::io::Cursor::new(data), None)?;
        let mut out = Vec::new();
        for b in reader {
            out.push(b?);
        }
        Ok(out)
    }

    pub fn schema_of(&self, name: &str, version: i64) -> Result<Schema> {
        let (_vm, schema) = self.get_version(name, version)?;
        Ok(schema)
    }
}
