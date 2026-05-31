use crate::models::{
    BarcodeType, DailyTarget, ExportConfig, HourStatistic, ScanRecord, ScanStatistics,
    TypeStatistic,
};
use chrono::{DateTime, Duration, Local, NaiveDate, NaiveDateTime, Utc};
use rusqlite::{params, Connection, Result};
use std::sync::Mutex;
use uuid::Uuid;

pub struct Database {
    conn: Mutex<Connection>,
}

impl Database {
    pub fn new(db_path: &str) -> Result<Self> {
        let conn = Connection::open(db_path)?;
        conn.execute_batch("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;")?;

        let db = Self {
            conn: Mutex::new(conn),
        };
        db.init_tables()?;
        Ok(db)
    }

    fn init_tables(&self) -> Result<()> {
        let conn = self.conn.lock().unwrap();

        conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS scan_records (
                id TEXT PRIMARY KEY,
                device_id TEXT NOT NULL,
                barcode TEXT NOT NULL,
                barcode_type TEXT NOT NULL,
                timestamp TEXT NOT NULL,
                success INTEGER NOT NULL DEFAULT 1,
                duration_ms INTEGER NOT NULL DEFAULT 0,
                created_at TEXT NOT NULL DEFAULT (datetime('now'))
            );

            CREATE INDEX IF NOT EXISTS idx_scan_records_timestamp
                ON scan_records(timestamp);

            CREATE INDEX IF NOT EXISTS idx_scan_records_device_id
                ON scan_records(device_id);

            CREATE INDEX IF NOT EXISTS idx_scan_records_barcode_type
                ON scan_records(barcode_type);

            CREATE TABLE IF NOT EXISTS app_config (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL,
                updated_at TEXT NOT NULL DEFAULT (datetime('now'))
            );

            INSERT OR IGNORE INTO app_config (key, value)
                VALUES ('daily_target', '5000');
            ",
        )?;

        Ok(())
    }

    pub fn insert_scan_record(&self, record: &ScanRecord) -> Result<()> {
        let conn = self.conn.lock().unwrap();
        conn.execute(
            "INSERT INTO scan_records (id, device_id, barcode, barcode_type, timestamp, success, duration_ms)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            params![
                record.id.to_string(),
                record.device_id.to_string(),
                record.barcode,
                record.barcode_type.display_name(),
                record.timestamp,
                record.success as i32,
                record.duration_ms as i64,
            ],
        )?;
        Ok(())
    }

    pub fn get_recent_records(&self, limit: i32) -> Result<Vec<ScanRecord>> {
        let conn = self.conn.lock().unwrap();
        let mut stmt = conn.prepare(
            "SELECT id, device_id, barcode, barcode_type, timestamp, success, duration_ms
             FROM scan_records
             ORDER BY timestamp DESC
             LIMIT ?1",
        )?;

        let records = stmt
            .query_map(params![limit], |row| {
                let id_str: String = row.get(0)?;
                let device_id_str: String = row.get(1)?;
                let type_name: String = row.get(3)?;

                Ok(ScanRecord {
                    id: Uuid::parse_str(&id_str).unwrap_or_default(),
                    device_id: Uuid::parse_str(&device_id_str).unwrap_or_default(),
                    barcode: row.get(2)?,
                    barcode_type: parse_barcode_type(&type_name),
                    timestamp: row.get(4)?,
                    success: row.get::<_, i32>(5)? != 0,
                    duration_ms: row.get::<_, i64>(6)? as u64,
                })
            })?
            .filter_map(|r| r.ok())
            .collect();

        Ok(records)
    }

    pub fn get_records_by_date_range(
        &self,
        start_date: &str,
        end_date: &str,
    ) -> Result<Vec<ScanRecord>> {
        let conn = self.conn.lock().unwrap();
        let mut stmt = conn.prepare(
            "SELECT id, device_id, barcode, barcode_type, timestamp, success, duration_ms
             FROM scan_records
             WHERE timestamp >= ?1 AND timestamp <= ?2
             ORDER BY timestamp DESC",
        )?;

        let records = stmt
            .query_map(params![start_date, end_date], |row| {
                let id_str: String = row.get(0)?;
                let device_id_str: String = row.get(1)?;
                let type_name: String = row.get(3)?;

                Ok(ScanRecord {
                    id: Uuid::parse_str(&id_str).unwrap_or_default(),
                    device_id: Uuid::parse_str(&device_id_str).unwrap_or_default(),
                    barcode: row.get(2)?,
                    barcode_type: parse_barcode_type(&type_name),
                    timestamp: row.get(4)?,
                    success: row.get::<_, i32>(5)? != 0,
                    duration_ms: row.get::<_, i64>(6)? as u64,
                })
            })?
            .filter_map(|r| r.ok())
            .collect();

        Ok(records)
    }

    pub fn get_statistics(&self) -> Result<ScanStatistics> {
        let conn = self.conn.lock().unwrap();

        let total_count: u64 = conn
            .query_row("SELECT COUNT(*) FROM scan_records", [], |row| {
                row.get::<_, i64>(0)
            })?
            .max(0) as u64;

        let success_count: u64 = conn
            .query_row(
                "SELECT COUNT(*) FROM scan_records WHERE success = 1",
                [],
                |row| row.get::<_, i64>(0),
            )?
            .max(0) as u64;

        let failed_count = total_count.saturating_sub(success_count);
        let success_rate = if total_count > 0 {
            (success_count as f64 / total_count as f64) * 100.0
        } else {
            0.0
        };

        let today = Local::now().format("%Y-%m-%d").to_string();
        let today_start = format!("{} 00:00:00", today);
        let today_end = format!("{} 23:59:59", today);

        let today_count: u64 = conn
            .query_row(
                "SELECT COUNT(*) FROM scan_records WHERE timestamp >= ?1 AND timestamp <= ?2",
                params![today_start, today_end],
                |row| row.get::<_, i64>(0),
            )?
            .max(0) as u64;

        let today_success: u64 = conn
            .query_row(
                "SELECT COUNT(*) FROM scan_records WHERE success = 1 AND timestamp >= ?1 AND timestamp <= ?2",
                params![today_start, today_end],
                |row| row.get::<_, i64>(0),
            )?
            .max(0) as u64;

        let today_failed = today_count.saturating_sub(today_success);

        let avg_interval_ms = self.calculate_avg_interval(&conn, &today_start, &today_end)?;

        let daily_target = self.get_daily_target_internal(&conn)?;
        let today_progress = if daily_target > 0 {
            (today_count as f64 / daily_target as f64) * 100.0
        } else {
            0.0
        };

        let by_type = self.get_type_statistics(&conn, &today_start, &today_end)?;
        let by_hour = self.get_hour_statistics(&conn, &today_start, &today_end)?;

        Ok(ScanStatistics {
            total_count,
            success_count,
            failed_count,
            success_rate,
            avg_interval_ms,
            today_count,
            today_success,
            today_failed,
            today_target: daily_target,
            today_progress,
            by_type,
            by_hour,
        })
    }

    fn calculate_avg_interval(
        &self,
        conn: &Connection,
        start: &str,
        end: &str,
    ) -> Result<f64> {
        let mut stmt = conn.prepare(
            "SELECT timestamp FROM scan_records
             WHERE timestamp >= ?1 AND timestamp <= ?2 AND success = 1
             ORDER BY timestamp ASC",
        )?;

        let timestamps: Vec<String> = stmt
            .query_map(params![start, end], |row| row.get(0))?
            .filter_map(|r| r.ok())
            .collect();

        if timestamps.len() < 2 {
            return Ok(0.0);
        }

        let mut total_interval_ms: i64 = 0;
        let mut count: i64 = 0;

        for i in 1..timestamps.len() {
            if let (Ok(t1), Ok(t2)) = (
                DateTime::parse_from_rfc3339(&timestamps[i - 1]),
                DateTime::parse_from_rfc3339(&timestamps[i]),
            ) {
                let diff = (t2 - t1).num_milliseconds();
                if diff > 0 && diff < 3600000 {
                    total_interval_ms += diff;
                    count += 1;
                }
            }
        }

        if count > 0 {
            Ok(total_interval_ms as f64 / count as f64)
        } else {
            Ok(0.0)
        }
    }

    fn get_type_statistics(
        &self,
        conn: &Connection,
        start: &str,
        end: &str,
    ) -> Result<Vec<TypeStatistic>> {
        let mut stmt = conn.prepare(
            "SELECT barcode_type, COUNT(*) as cnt
             FROM scan_records
             WHERE timestamp >= ?1 AND timestamp <= ?2
             GROUP BY barcode_type
             ORDER BY cnt DESC",
        )?;

        let today_total: u64 = conn
            .query_row(
                "SELECT COUNT(*) FROM scan_records WHERE timestamp >= ?1 AND timestamp <= ?2",
                params![start, end],
                |row| row.get::<_, i64>(0),
            )?
            .max(0) as u64;

        let stats = stmt
            .query_map(params![start, end], |row| {
                let type_name: String = row.get(0)?;
                let count: i64 = row.get(1)?;
                let count = count.max(0) as u64;
                let percentage = if today_total > 0 {
                    (count as f64 / today_total as f64) * 100.0
                } else {
                    0.0
                };

                Ok(TypeStatistic {
                    barcode_type: parse_barcode_type(&type_name),
                    type_name: type_name.clone(),
                    count,
                    percentage,
                })
            })?
            .filter_map(|r| r.ok())
            .collect();

        Ok(stats)
    }

    fn get_hour_statistics(
        &self,
        conn: &Connection,
        start: &str,
        end: &str,
    ) -> Result<Vec<HourStatistic>> {
        let mut stmt = conn.prepare(
            "SELECT CAST(strftime('%H', timestamp) AS INTEGER) as hour, COUNT(*) as cnt
             FROM scan_records
             WHERE timestamp >= ?1 AND timestamp <= ?2
             GROUP BY hour
             ORDER BY hour",
        )?;

        let stats = stmt
            .query_map(params![start, end], |row| {
                Ok(HourStatistic {
                    hour: row.get::<_, i32>(0)? as u32,
                    count: row.get::<_, i64>(1)?.max(0) as u64,
                })
            })?
            .filter_map(|r| r.ok())
            .collect();

        Ok(stats)
    }

    pub fn get_daily_target(&self) -> Result<u64> {
        let conn = self.conn.lock().unwrap();
        self.get_daily_target_internal(&conn)
    }

    fn get_daily_target_internal(&self, conn: &Connection) -> Result<u64> {
        let result = conn.query_row(
            "SELECT value FROM app_config WHERE key = 'daily_target'",
            [],
            |row| row.get::<_, String>(0),
        );

        match result {
            Ok(val) => Ok(val.parse::<u64>().unwrap_or(5000)),
            Err(_) => Ok(5000),
        }
    }

    pub fn set_daily_target(&self, target: u64) -> Result<()> {
        let conn = self.conn.lock().unwrap();
        conn.execute(
            "INSERT OR REPLACE INTO app_config (key, value, updated_at)
             VALUES ('daily_target', ?1, datetime('now'))",
            params![target.to_string()],
        )?;
        Ok(())
    }

    pub fn get_daily_target_info(&self) -> Result<DailyTarget> {
        let conn = self.conn.lock().unwrap();
        let target = self.get_daily_target_internal(&conn)?;

        let today = Local::now().format("%Y-%m-%d").to_string();
        let today_start = format!("{} 00:00:00", today);
        let today_end = format!("{} 23:59:59", today);

        let current: u64 = conn
            .query_row(
                "SELECT COUNT(*) FROM scan_records WHERE timestamp >= ?1 AND timestamp <= ?2",
                params![today_start, today_end],
                |row| row.get::<_, i64>(0),
            )?
            .max(0) as u64;

        let progress = if target > 0 {
            (current as f64 / target as f64) * 100.0
        } else {
            0.0
        };

        Ok(DailyTarget {
            date: today,
            target,
            current,
            progress,
        })
    }

    pub fn get_history_days(&self, days: i32) -> Result<Vec<HourStatistic>> {
        let conn = self.conn.lock().unwrap();
        let start_date = (Local::now() - Duration::days(days as i64))
            .format("%Y-%m-%d 00:00:00")
            .to_string();
        let end_date = Local::now().format("%Y-%m-%d 23:59:59").to_string();

        self.get_hour_statistics(&conn, &start_date, &end_date)
    }

    pub fn delete_all_records(&self) -> Result<()> {
        let conn = self.conn.lock().unwrap();
        conn.execute("DELETE FROM scan_records", [])?;
        Ok(())
    }

    pub fn export_to_csv(&self, output_path: &str) -> Result<usize> {
        let conn = self.conn.lock().unwrap();

        let mut stmt = conn.prepare(
            "SELECT id, device_id, barcode, barcode_type, timestamp, success, duration_ms
             FROM scan_records
             ORDER BY timestamp DESC",
        )?;

        let records: Vec<ScanRecord> = stmt
            .query_map([], |row| {
                let id_str: String = row.get(0)?;
                let device_id_str: String = row.get(1)?;
                let type_name: String = row.get(3)?;

                Ok(ScanRecord {
                    id: Uuid::parse_str(&id_str).unwrap_or_default(),
                    device_id: Uuid::parse_str(&device_id_str).unwrap_or_default(),
                    barcode: row.get(2)?,
                    barcode_type: parse_barcode_type(&type_name),
                    timestamp: row.get(4)?,
                    success: row.get::<_, i32>(5)? != 0,
                    duration_ms: row.get::<_, i64>(6)? as u64,
                })
            })?
            .filter_map(|r| r.ok())
            .collect();

        let mut writer = csv::Writer::from_path(output_path)
            .map_err(|e| rusqlite::Error::SqliteFailure(
                rusqlite::ffi::Error::new(1),
                Some(format!("CSV error: {}", e)),
            ))?;

        writer
            .write_record(["ID", "设备ID", "条码内容", "条码类型", "时间戳", "是否成功", "耗时(ms)"])
            .map_err(|e| rusqlite::Error::SqliteFailure(
                rusqlite::ffi::Error::new(1),
                Some(format!("CSV write error: {}", e)),
            ))?;

        for record in &records {
            writer
                .write_record([
                    record.id.to_string(),
                    record.device_id.to_string(),
                    record.barcode.clone(),
                    record.barcode_type.display_name().to_string(),
                    record.timestamp.clone(),
                    if record.success { "成功" } else { "失败" }.to_string(),
                    record.duration_ms.to_string(),
                ])
                .map_err(|e| rusqlite::Error::SqliteFailure(
                    rusqlite::ffi::Error::new(1),
                    Some(format!("CSV write error: {}", e)),
                ))?;
        }

        writer.flush().map_err(|e| rusqlite::Error::SqliteFailure(
            rusqlite::ffi::Error::new(1),
            Some(format!("CSV flush error: {}", e)),
        ))?;

        Ok(records.len())
    }
}

fn parse_barcode_type(name: &str) -> BarcodeType {
    match name {
        "EAN-13" => BarcodeType::EAN13,
        "Code 128" => BarcodeType::Code128,
        "QR Code" => BarcodeType::QRCode,
        "Code 39" => BarcodeType::Code39,
        "EAN-8" => BarcodeType::EAN8,
        "UPC-A" => BarcodeType::UPCA,
        "UPC-E" => BarcodeType::UPCE,
        "Code 93" => BarcodeType::Code93,
        "ITF" => BarcodeType::ITF,
        "Codabar" => BarcodeType::Codabar,
        "Data Matrix" => BarcodeType::DataMatrix,
        "PDF417" => BarcodeType::PDF417,
        _ => BarcodeType::Unknown,
    }
}
