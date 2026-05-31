// Prevents additional console window on Windows in release.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod error;
mod frame;
mod gesture;
mod inference;
mod recording;
mod server;
mod state;

use state::AppState;
use tauri::Manager;

fn main() -> tauri::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .with_target(false)
        .init();

    let state = AppState::new();

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .manage(state.clone())
        .invoke_handler(tauri::generate_handler![
            server::start_ws_server,
            server::get_ws_port,
            inference::load_model,
            inference::unload_model,
            inference::get_model_status,
            recording::save_recording,
            recording::list_recordings,
            gesture::list_sequences_cmd,
            gesture::save_sequences_cmd,
            gesture::add_sequence_cmd,
            gesture::delete_sequence_cmd,
            gesture::match_sequences_cmd,
            gesture::trigger_action_cmd,
            gesture::get_trajectory_cmd,
            gesture::clear_trajectory_cmd,
            gesture::get_default_templates_cmd,
        ])
        .setup(move |app| {
            let handle = app.handle().clone();
            let state_for_server = state.clone();
            tauri::async_runtime::spawn(async move {
                if let Err(e) = server::run_server(&handle, state_for_server).await {
                    tracing::error!("WebSocket server exited with error: {e}");
                }
            });
            Ok(())
        })
        .run(tauri::generate_context!())
}
