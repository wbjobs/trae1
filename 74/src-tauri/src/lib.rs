#![cfg_attr(
    all(not(debug_assertions), target_os = "windows"),
    windows_subsystem = "windows"
)]

pub mod commands;
pub mod database;
pub mod hid_manager;
pub mod models;

use database::Database;
use hid_manager::HidManager;
use parking_lot::Mutex;
use std::sync::Arc;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();

    let app_data_dir = tauri::api::path::app_data_dir(&tauri::generate_context!().config())
        .unwrap_or_else(|| std::path::PathBuf::from("."));
    std::fs::create_dir_all(&app_data_dir).ok();
    let db_path = app_data_dir.join("scanner_data.db");
    let db_path_str = db_path.to_string_lossy().to_string();

    log::info!("Database path: {}", db_path_str);

    let database = match Database::new(&db_path_str) {
        Ok(db) => {
            log::info!("Database initialized successfully");
            Arc::new(Mutex::new(db))
        }
        Err(e) => {
            log::error!("Failed to initialize database: {}", e);
            panic!("Failed to initialize database: {}", e);
        }
    };

    let hid_manager = Arc::new(Mutex::new(HidManager::new()));

    tauri::Builder::default()
        .manage(hid_manager)
        .manage(database)
        .invoke_handler(tauri::generate_handler![
            commands::list_devices,
            commands::connect_device,
            commands::disconnect_device,
            commands::get_device_config,
            commands::get_device_capabilities,
            commands::set_device_config,
            commands::get_connected_devices,
            commands::apply_preset,
            commands::backup_config,
            commands::restore_config,
            commands::start_scan,
            commands::stop_scan,
            commands::add_scan_record,
            commands::get_recent_records,
            commands::get_records_by_date,
            commands::get_scan_statistics,
            commands::get_daily_target_info,
            commands::set_daily_target,
            commands::get_history_days,
            commands::export_records_csv,
            commands::delete_all_records,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
