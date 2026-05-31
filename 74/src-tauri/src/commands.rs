use crate::database::Database;
use crate::hid_manager::HidManager;
use crate::models::{
    BackupData, DailyTarget, DeviceBackup, DeviceCapabilities, DeviceInfo, PresetTemplate,
    ScanRecord, ScanStatistics, ScannerConfig, WriteResult,
};
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tauri::State;
use uuid::Uuid;

type HidManagerState = State<'_, Arc<Mutex<HidManager>>>;
type DatabaseState = State<'_, Arc<Mutex<Database>>>;

#[derive(Serialize, Deserialize)]
pub struct AddScanRequest {
    pub device_id: Uuid,
    pub barcode: String,
    pub success: bool,
    pub duration_ms: u64,
}

#[derive(Serialize, Deserialize)]
pub struct SetTargetRequest {
    pub target: u64,
}

#[derive(Serialize, Deserialize)]
pub struct ExportCsvRequest {
    pub file_path: String,
}

#[derive(Serialize, Deserialize)]
pub struct DateRangeRequest {
    pub start_date: String,
    pub end_date: String,
}

#[derive(Serialize, Deserialize)]
pub struct ConnectRequest {
    pub vendor_id: u16,
    pub product_id: u16,
    pub path: String,
}

#[derive(Serialize, Deserialize)]
pub struct DeviceIdRequest {
    pub device_id: Uuid,
}

#[derive(Serialize, Deserialize)]
pub struct ConfigRequest {
    pub device_id: Uuid,
    pub config: ScannerConfig,
}

#[derive(Serialize, Deserialize)]
pub struct PresetRequest {
    pub device_id: Uuid,
    pub preset_name: String,
}

#[derive(Serialize, Deserialize)]
pub struct BackupRequest {
    pub file_path: String,
}

#[derive(Serialize, Deserialize)]
pub struct RestoreRequest {
    pub file_path: String,
}

#[tauri::command]
pub async fn list_devices(manager: HidManagerState) -> Result<Vec<DeviceInfo>, String> {
    let mut mgr = manager.lock();
    mgr.list_devices()
}

#[tauri::command]
pub async fn connect_device(
    manager: HidManagerState,
    request: ConnectRequest,
) -> Result<DeviceInfo, String> {
    let mut mgr = manager.lock();
    mgr.connect(request.vendor_id, request.product_id, &request.path)
}

#[tauri::command]
pub async fn disconnect_device(
    manager: HidManagerState,
    request: DeviceIdRequest,
) -> Result<(), String> {
    let mut mgr = manager.lock();
    mgr.disconnect(request.device_id)
}

#[tauri::command]
pub async fn get_device_config(
    manager: HidManagerState,
    request: DeviceIdRequest,
) -> Result<ScannerConfig, String> {
    let mgr = manager.lock();
    mgr.get_config(request.device_id)
}

#[tauri::command]
pub async fn get_device_capabilities(
    manager: HidManagerState,
    request: DeviceIdRequest,
) -> Result<DeviceCapabilities, String> {
    let mgr = manager.lock();
    mgr.get_capabilities(request.device_id)
}

#[tauri::command]
pub async fn set_device_config(
    manager: HidManagerState,
    request: ConfigRequest,
) -> Result<WriteResult, String> {
    let mut mgr = manager.lock();
    mgr.set_config(request.device_id, request.config)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn get_connected_devices(
    manager: HidManagerState,
) -> Result<Vec<DeviceInfo>, String> {
    let mgr = manager.lock();
    Ok(mgr.get_connected_devices())
}

#[tauri::command]
pub async fn apply_preset(
    manager: HidManagerState,
    request: PresetRequest,
) -> Result<WriteResult, String> {
    let presets = crate::models::get_preset_templates();
    let preset = presets
        .iter()
        .find(|p| p.name == request.preset_name)
        .ok_or_else(|| format!("Preset '{}' not found", request.preset_name))?;

    let mut mgr = manager.lock();
    mgr.set_config(request.device_id, preset.config.clone())
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn backup_config(
    manager: HidManagerState,
    request: BackupRequest,
) -> Result<String, String> {
    let mgr = manager.lock();
    let connected = mgr.get_connected_devices();

    let mut devices = std::collections::HashMap::new();
    for device_info in connected {
        if let Ok(config) = mgr.get_config(device_info.id) {
            devices.insert(
                device_info.id,
                DeviceBackup {
                    device_info: device_info.clone(),
                    config,
                },
            );
        }
    }

    let backup = BackupData {
        timestamp: chrono_like_timestamp(),
        devices,
    };

    let json =
        serde_json::to_string_pretty(&backup).map_err(|e| format!("Failed to serialize backup: {}", e))?;

    std::fs::write(&request.file_path, json)
        .map_err(|e| format!("Failed to write backup file: {}", e))?;

    Ok(request.file_path)
}

#[tauri::command]
pub async fn restore_config(
    manager: HidManagerState,
    request: RestoreRequest,
) -> Result<Vec<DeviceInfo>, String> {
    let content = std::fs::read_to_string(&request.file_path)
        .map_err(|e| format!("Failed to read backup file: {}", e))?;

    let backup: BackupData =
        serde_json::from_str(&content).map_err(|e| format!("Failed to parse backup file: {}", e))?;

    let mut mgr = manager.lock();
    let mut restored = Vec::new();

    for (_id, device_backup) in backup.devices {
        let info = device_backup.device_info;
        match mgr.connect(info.vendor_id, info.product_id, &info.path) {
            Ok(connected_info) => {
                let _ = mgr.set_config(connected_info.id, device_backup.config);
                restored.push(connected_info);
            }
            Err(e) => {
                log::warn!("Failed to restore device {:?}: {}", info.product, e);
            }
        }
    }

    Ok(restored)
}

#[tauri::command]
pub async fn start_scan(
    manager: HidManagerState,
    request: DeviceIdRequest,
) -> Result<(), String> {
    let mgr = manager.lock();
    if !mgr.is_connected(request.device_id) {
        return Err("Device not connected".to_string());
    }
    Ok(())
}

#[tauri::command]
pub async fn stop_scan(
    manager: HidManagerState,
    request: DeviceIdRequest,
) -> Result<(), String> {
    let mgr = manager.lock();
    if !mgr.is_connected(request.device_id) {
        return Err("Device not connected".to_string());
    }
    Ok(())
}

fn chrono_like_timestamp() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    format!("{}", secs)
}

#[tauri::command]
pub async fn add_scan_record(
    db: DatabaseState,
    request: AddScanRequest,
) -> Result<ScanRecord, String> {
    let barcode_type = crate::models::BarcodeType::from_barcode(&request.barcode);
    let record = ScanRecord {
        id: Uuid::new_v4(),
        device_id: request.device_id,
        barcode: request.barcode.clone(),
        barcode_type: barcode_type.clone(),
        timestamp: chrono::Local::now().to_rfc3339(),
        success: request.success,
        duration_ms: request.duration_ms,
    };

    let db = db.lock();
    db.insert_scan_record(&record)
        .map_err(|e| format!("Failed to insert scan record: {}", e))?;

    Ok(record)
}

#[tauri::command]
pub async fn get_recent_records(
    db: DatabaseState,
    limit: i32,
) -> Result<Vec<ScanRecord>, String> {
    let db = db.lock();
    db.get_recent_records(limit)
        .map_err(|e| format!("Failed to get recent records: {}", e))
}

#[tauri::command]
pub async fn get_records_by_date(
    db: DatabaseState,
    request: DateRangeRequest,
) -> Result<Vec<ScanRecord>, String> {
    let db = db.lock();
    db.get_records_by_date_range(&request.start_date, &request.end_date)
        .map_err(|e| format!("Failed to get records: {}", e))
}

#[tauri::command]
pub async fn get_scan_statistics(
    db: DatabaseState,
) -> Result<ScanStatistics, String> {
    let db = db.lock();
    db.get_statistics()
        .map_err(|e| format!("Failed to get statistics: {}", e))
}

#[tauri::command]
pub async fn get_daily_target_info(
    db: DatabaseState,
) -> Result<DailyTarget, String> {
    let db = db.lock();
    db.get_daily_target_info()
        .map_err(|e| format!("Failed to get daily target: {}", e))
}

#[tauri::command]
pub async fn set_daily_target(
    db: DatabaseState,
    request: SetTargetRequest,
) -> Result<(), String> {
    let db = db.lock();
    db.set_daily_target(request.target)
        .map_err(|e| format!("Failed to set daily target: {}", e))
}

#[tauri::command]
pub async fn get_history_days(
    db: DatabaseState,
    days: i32,
) -> Result<Vec<crate::models::HourStatistic>, String> {
    let db = db.lock();
    db.get_history_days(days)
        .map_err(|e| format!("Failed to get history: {}", e))
}

#[tauri::command]
pub async fn export_records_csv(
    db: DatabaseState,
    request: ExportCsvRequest,
) -> Result<usize, String> {
    let db = db.lock();
    db.export_to_csv(&request.file_path)
        .map_err(|e| format!("Failed to export CSV: {}", e))
}

#[tauri::command]
pub async fn delete_all_records(
    db: DatabaseState,
) -> Result<(), String> {
    let db = db.lock();
    db.delete_all_records()
        .map_err(|e| format!("Failed to delete records: {}", e))
}
