use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use rusqlite::{Connection, OpenFlags};
use tokio::sync::mpsc;
use tokio::sync::oneshot;
use tracing::{debug, info, warn};

use crate::error::AppError;

pub type PoolResult<T> = Result<T, AppError>;

const DEFAULT_READ_POOL_SIZE: usize = 4;
const DEFAULT_MAX_BUSY_RETRIES: u32 = 5;
const DEFAULT_WAL_AUTO_CHECKPOINT_BYTES: u64 = 100 * 1024 * 1024;

static GENERATION: AtomicU64 = AtomicU64::new(0);

pub fn current_generation() -> u64 {
    GENERATION.load(Ordering::SeqCst)
}

pub(crate) fn bump_generation() {
    GENERATION.fetch_add(1, Ordering::SeqCst);
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Role {
    Master,
    Slave,
}

impl Role {
    pub fn as_str(&self) -> &'static str {
        match self {
            Role::Master => "master",
            Role::Slave => "slave",
        }
    }
}

#[derive(Clone)]
struct PoolInner {
    readers: Arc<Vec<Mutex<Connection>>>,
    write_tx: Option<mpsc::UnboundedSender<WriteRequest>>,
    role: Role,
}

#[derive(Clone)]
pub struct SqlitePool {
    inner: Arc<std::sync::RwLock<PoolInner>>,
    pub max_busy_retries: u32,
    db_path: PathBuf,
    wal_path: PathBuf,
    wal_auto_checkpoint_bytes: u64,
    read_pool_size: usize,
    pub master_url: Arc<std::sync::RwLock<Option<String>>>,
}

impl SqlitePool {
    pub fn builder() -> PoolBuilder {
        PoolBuilder::default()
    }

    pub fn role(&self) -> Role {
        self.inner.read().unwrap().role
    }

    pub async fn read<F, R>(&self, f: F) -> PoolResult<R>
    where
        F: FnOnce(&Connection) -> Result<R, rusqlite::Error> + Send + 'static,
        R: Send + 'static,
    {
        let readers = self.inner.read().unwrap().readers.clone();
        let retries = self.max_busy_retries;
        let pool_size = self.read_pool_size;
        tokio::task::spawn_blocking(move || {
            for attempt in 0..=retries {
                if attempt > 0 {
                    let backoff =
                        Duration::from_millis(10 * (1u64 << (attempt - 1).min(6)));
                    std::thread::sleep(backoff);
                }
                let idx = pick_reader(pool_size);
                let conn = readers[idx].lock().unwrap();
                match f(&conn) {
                    Ok(r) => return Ok(r),
                    Err(rusqlite::Error::SqliteFailure(err, _))
                        if err.code == rusqlite::ErrorCode::DatabaseBusy
                            && attempt < retries =>
                    {
                        debug!(attempt, "read busy, retrying");
                        continue;
                    }
                    Err(e) => return Err(AppError::Sql(e)),
                }
            }
            unreachable!()
        })
        .await
        .map_err(|e| AppError::Pool(e.to_string()))?
    }

    pub async fn write<F, R>(&self, f: F) -> PoolResult<R>
    where
        F: FnOnce(&Connection) -> Result<R, rusqlite::Error> + Send + 'static,
        R: Send + 'static,
    {
        let write_tx = {
            let inner = self.inner.read().unwrap();
            if inner.role != Role::Master {
                return Err(AppError::Pool(
                    "node is not master; writes not allowed".into(),
                ));
            }
            inner
                .write_tx
                .clone()
                .ok_or_else(|| AppError::Pool("node has no write channel".into()))?
        };

        let (tx, rx) = oneshot::channel();
        let req = WriteRequest::Execute(Box::new(move |conn| {
            let res = f(conn);
            if res.is_ok() {
                bump_generation();
            }
            let _ = tx.send(res);
        }));
        write_tx
            .send(req)
            .map_err(|_| AppError::Pool("write worker stopped".into()))?;
        rx.await
            .map_err(|_| AppError::Pool("write worker dropped response".into()))?
            .map_err(AppError::Sql)
    }

    pub async fn trigger_checkpoint(&self, truncate: bool) -> PoolResult<CheckpointResult> {
        let write_tx = {
            let inner = self.inner.read().unwrap();
            inner
                .write_tx
                .clone()
                .ok_or_else(|| AppError::Pool("no write channel".into()))?
        };
        let (tx, rx) = oneshot::channel();
        let req = WriteRequest::Checkpoint {
            truncate,
            reply: tx,
        };
        write_tx
            .send(req)
            .map_err(|_| AppError::Pool("write worker stopped".into()))?;
        rx.await
            .map_err(|_| AppError::Pool("checkpoint response dropped".into()))?
    }

    pub fn wal_size_on_disk(&self) -> u64 {
        fs::metadata(&self.wal_path).map(|m| m.len()).unwrap_or(0)
    }

    pub fn db_path(&self) -> &Path {
        &self.db_path
    }

    pub fn export_snapshot(&self) -> PoolResult<Vec<u8>> {
        let tmp_path = self.db_path.with_extension("snap.tmp");
        {
            let inner = self.inner.read().unwrap();
            let conn = inner.readers[0].lock().unwrap();
            conn.pragma_update(None, "wal_checkpoint", "TRUNCATE")
                .map_err(AppError::Sql)?;
            let tmp = Connection::open_with_flags(
                &tmp_path,
                OpenFlags::SQLITE_OPEN_READ_WRITE
                    | OpenFlags::SQLITE_OPEN_CREATE
                    | OpenFlags::SQLITE_OPEN_URI
                    | OpenFlags::SQLITE_OPEN_NO_MUTEX,
            )
            .map_err(AppError::Sql)?;
            rusqlite::backup::Backup::new(
                rusqlite::DatabaseName::Main,
                &conn,
                rusqlite::DatabaseName::Main,
                &tmp,
            )
            .and_then(|b| b.run_to_completion(1000, Duration::from_millis(250), None))
            .map_err(AppError::Sql)?;
        }
        let bytes = fs::read(&tmp_path).map_err(|e| AppError::Pool(e.to_string()))?;
        let _ = fs::remove_file(&tmp_path);
        Ok(bytes)
    }

    pub fn apply_snapshot_bytes(&self, bytes: &[u8]) -> PoolResult<()> {
        let tmp_path = self.db_path.with_extension("snap.tmp");
        fs::write(&tmp_path, bytes).map_err(|e| AppError::Pool(e.to_string()))?;

        let inner = self.inner.read().unwrap();
        let conn = inner.readers[0].lock().unwrap();

        let src = Connection::open_with_flags(
            &tmp_path,
            OpenFlags::SQLITE_OPEN_READ_ONLY
                | OpenFlags::SQLITE_OPEN_URI
                | OpenFlags::SQLITE_OPEN_NO_MUTEX,
        )
        .map_err(AppError::Sql)?;

        rusqlite::backup::Backup::new(
            rusqlite::DatabaseName::Main,
            &src,
            rusqlite::DatabaseName::Main,
            &conn,
        )
        .and_then(|b| b.run_to_completion(1000, Duration::from_millis(250), None))
        .map_err(AppError::Sql)?;

        drop(src);
        drop(conn);
        drop(inner);

        let _ = fs::remove_file(&tmp_path);
        let _ = fs::remove_file(&self.wal_path);
        let shm = PathBuf::from(format!("{}-shm", self.db_path.display()));
        let _ = fs::remove_file(&shm);
        Ok(())
    }

    pub async fn promote_to_master(&self) -> PoolResult<()> {
        let mut inner = self.inner.write().unwrap();
        if inner.role == Role::Master {
            return Ok(());
        }

        let writer_conn = open_connection(&self.db_path, true)?;
        configure_wal(&writer_conn)?;
        set_pragmas(&writer_conn)?;

        let (write_tx, mut write_rx) = mpsc::unbounded_channel::<WriteRequest>();
        let wal_path_for_worker = self.wal_path.clone();
        let wal_auto = self.wal_auto_checkpoint_bytes;

        std::thread::Builder::new()
            .name("sqlite-write-worker".into())
            .spawn(move || {
                let writer_conn = writer_conn;
                while let Some(req) = write_rx.blocking_recv() {
                    match req {
                        WriteRequest::Execute(work) => {
                            work(&writer_conn);
                            if let Err(e) = maybe_auto_checkpoint(
                                &writer_conn,
                                &wal_path_for_worker,
                                wal_auto,
                            ) {
                                warn!(error = %e, "auto checkpoint failed");
                            }
                        }
                        WriteRequest::Checkpoint { truncate, reply } => {
                            let res =
                                run_checkpoint(&writer_conn, &wal_path_for_worker, truncate);
                            let _ = reply.send(res);
                        }
                    }
                }
                info!("sqlite write worker exiting");
            })
            .map_err(|e| AppError::Pool(e.to_string()))?;

        inner.write_tx = Some(write_tx);
        inner.role = Role::Master;
        *self.master_url.write().unwrap() = None;
        info!("promoted to master");
        Ok(())
    }

    pub async fn demote_to_slave(&self) -> PoolResult<()> {
        let mut inner = self.inner.write().unwrap();
        if inner.role == Role::Slave {
            return Ok(());
        }
        if let Some(tx) = inner.write_tx.take() {
            drop(tx);
        }
        inner.role = Role::Slave;
        info!("demoted to slave");
        Ok(())
    }
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct CheckpointResult {
    pub truncated: bool,
    pub before_bytes: u64,
    pub after_bytes: u64,
    pub checkpointed_pages: i64,
    pub synced_pages: i64,
}

fn pick_reader(n: usize) -> usize {
    use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};
    static COUNTER: AtomicUsize = AtomicUsize::new(0);
    COUNTER.fetch_add(1, AtomicOrdering::Relaxed) % n
}

type WriteFn = Box<dyn FnOnce(&Connection) + Send>;

enum WriteRequest {
    Execute(WriteFn),
    Checkpoint {
        truncate: bool,
        reply: oneshot::Sender<PoolResult<CheckpointResult>>,
    },
}

pub struct PoolBuilder {
    path: Option<PathBuf>,
    read_pool_size: usize,
    max_busy_retries: u32,
    wal_auto_checkpoint_bytes: u64,
    role: Role,
    master_url: Option<String>,
}

impl Default for PoolBuilder {
    fn default() -> Self {
        PoolBuilder {
            path: None,
            read_pool_size: DEFAULT_READ_POOL_SIZE,
            max_busy_retries: DEFAULT_MAX_BUSY_RETRIES,
            wal_auto_checkpoint_bytes: DEFAULT_WAL_AUTO_CHECKPOINT_BYTES,
            role: Role::Master,
            master_url: None,
        }
    }
}

impl PoolBuilder {
    pub fn path(mut self, p: impl Into<PathBuf>) -> Self {
        self.path = Some(p.into());
        self
    }

    pub fn read_pool_size(mut self, n: usize) -> Self {
        self.read_pool_size = n.max(1);
        self
    }

    pub fn max_busy_retries(mut self, n: u32) -> Self {
        self.max_busy_retries = n;
        self
    }

    pub fn wal_auto_checkpoint_bytes(mut self, n: u64) -> Self {
        self.wal_auto_checkpoint_bytes = n;
        self
    }

    pub fn role(mut self, role: Role) -> Self {
        self.role = role;
        self
    }

    pub fn master_url(mut self, url: impl Into<String>) -> Self {
        self.master_url = Some(url.into());
        self
    }

    pub async fn build(self) -> PoolResult<SqlitePool> {
        let path = self
            .path
            .ok_or_else(|| AppError::Pool("database path is required".into()))?;

        let read_pool_size = self.read_pool_size;
        let max_busy_retries = self.max_busy_retries;
        let wal_auto_checkpoint_bytes = self.wal_auto_checkpoint_bytes;
        let wal_path = PathBuf::from(format!("{}-wal", path.display()));

        if self.role == Role::Master {
            let writer_conn = open_connection(&path, true)?;
            configure_wal(&writer_conn)?;
            set_pragmas(&writer_conn)?;
            drop(writer_conn);
        }

        let mut readers = Vec::with_capacity(read_pool_size);
        let reader_flags = OpenFlags::SQLITE_OPEN_READ_WRITE
            | OpenFlags::SQLITE_OPEN_CREATE
            | OpenFlags::SQLITE_OPEN_URI
            | OpenFlags::SQLITE_OPEN_NO_MUTEX;
        for _ in 0..read_pool_size {
            let conn = Connection::open_with_flags(&path, reader_flags)
                .map_err(AppError::Sql)?;
            set_pragmas(&conn)?;
            readers.push(Mutex::new(conn));
        }
        let readers_arc = Arc::new(readers);

        info!(
            readers = read_pool_size,
            path = %path.display(),
            role = self.role.as_str(),
            wal_auto_checkpoint_mb = wal_auto_checkpoint_bytes / (1024 * 1024),
            "sqlite pool built with WAL mode"
        );

        let write_tx = if self.role == Role::Master {
            let writer_conn = open_connection(&path, true)?;
            configure_wal(&writer_conn)?;
            set_pragmas(&writer_conn)?;

            let (write_tx, mut write_rx) = mpsc::unbounded_channel::<WriteRequest>();
            let wal_path_for_worker = wal_path.clone();

            std::thread::Builder::new()
                .name("sqlite-write-worker".into())
                .spawn(move || {
                    let writer_conn = writer_conn;
                    while let Some(req) = write_rx.blocking_recv() {
                        match req {
                            WriteRequest::Execute(work) => {
                                work(&writer_conn);
                                if let Err(e) = maybe_auto_checkpoint(
                                    &writer_conn,
                                    &wal_path_for_worker,
                                    wal_auto_checkpoint_bytes,
                                ) {
                                    warn!(error = %e, "auto checkpoint failed");
                                }
                            }
                            WriteRequest::Checkpoint { truncate, reply } => {
                                let res = run_checkpoint(
                                    &writer_conn,
                                    &wal_path_for_worker,
                                    truncate,
                                );
                                let _ = reply.send(res);
                            }
                        }
                    }
                    info!("sqlite write worker exiting");
                })
                .map_err(|e| AppError::Pool(e.to_string()))?;

            Some(write_tx)
        } else {
            None
        };

        Ok(SqlitePool {
            inner: Arc::new(std::sync::RwLock::new(PoolInner {
                readers: readers_arc,
                write_tx,
                role: self.role,
            })),
            max_busy_retries,
            db_path: path,
            wal_path,
            wal_auto_checkpoint_bytes,
            read_pool_size,
            master_url: Arc::new(std::sync::RwLock::new(self.master_url)),
        })
    }
}

fn maybe_auto_checkpoint(
    conn: &Connection,
    wal_path: &Path,
    threshold_bytes: u64,
) -> PoolResult<()> {
    let wal_size = fs::metadata(wal_path).map(|m| m.len()).unwrap_or(0);
    if wal_size >= threshold_bytes {
        warn!(
            wal_size_mb = wal_size / (1024 * 1024),
            threshold_mb = threshold_bytes / (1024 * 1024),
            "WAL exceeds threshold, running auto checkpoint"
        );
        let res = run_checkpoint(conn, wal_path, true)?;
        info!(
            before_mb = res.before_bytes / (1024 * 1024),
            after_mb = res.after_bytes / (1024 * 1024),
            pages = res.checkpointed_pages,
            "auto checkpoint complete"
        );
    }
    Ok(())
}

fn run_checkpoint(
    conn: &Connection,
    wal_path: &Path,
    truncate: bool,
) -> PoolResult<CheckpointResult> {
    let before = fs::metadata(wal_path).map(|m| m.len()).unwrap_or(0);

    let mode = if truncate { "TRUNCATE" } else { "PASSIVE" };
    let pragma_sql = format!("PRAGMA wal_checkpoint({});", mode);
    let (checkpointed_pages, synced_pages) = conn
        .query_row(&pragma_sql, [], |row| {
            Ok((row.get::<_, i64>(1)?, row.get::<_, i64>(2)?))
        })
        .map_err(AppError::Sql)?;

    std::thread::yield_now();
    let after = fs::metadata(wal_path).map(|m| m.len()).unwrap_or(0);

    Ok(CheckpointResult {
        truncated: truncate,
        before_bytes: before,
        after_bytes: after,
        checkpointed_pages,
        synced_pages,
    })
}

fn open_connection(path: &PathBuf, write: bool) -> Result<Connection, rusqlite::Error> {
    let mut flags = OpenFlags::SQLITE_OPEN_READ_WRITE | OpenFlags::SQLITE_OPEN_CREATE;
    if !write {
        flags = OpenFlags::SQLITE_OPEN_READ_ONLY;
    }
    flags |= OpenFlags::SQLITE_OPEN_URI;
    flags |= OpenFlags::SQLITE_OPEN_NO_MUTEX;
    Connection::open_with_flags(path, flags)
}

fn configure_wal(conn: &Connection) -> Result<(), rusqlite::Error> {
    conn.pragma_update(None, "journal_mode", "WAL")?;
    conn.pragma_update(None, "synchronous", "NORMAL")?;
    conn.pragma_update(None, "wal_autocheckpoint", 0)?;
    Ok(())
}

fn set_pragmas(conn: &Connection) -> Result<(), rusqlite::Error> {
    conn.pragma_update(None, "busy_timeout", 0)?;
    conn.pragma_update(None, "foreign_keys", "ON")?;
    Ok(())
}
