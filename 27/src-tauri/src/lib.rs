use crate::compression::{
    compress_point_cloud, decompress_point_cloud, serialize_compressed, deserialize_compressed,
};
use crate::downsampling::{adaptive_downsample, voxel_grid_downsample, compute_voxel_size_for_target};
use crate::icp_registration::{
    self, Correspondence, ICPConfig, RegistrationResult, RegistrationEvaluation,
    point_to_point_icp, manual_registration, evaluate_registration,
    merge_point_clouds, transform_point_cloud,
};
use crate::ply_loader::load_ply;
use crate::point_cloud::{PointCloud, Point, Color};
use crate::webrtc_signaling::{
    AppState, AppData, ConnectionInfo, TransferProgress, TransferStats, DataChannelState,
    configure_backpressure, pause_channel, resume_channel, get_buffer_status,
};
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::sync::Arc;
use tauri::{Emitter, Manager, State};
use uuid::Uuid;

pub mod compression;
pub mod downsampling;
pub mod icp_registration;
pub mod ply_loader;
pub mod point_cloud;
pub mod webrtc_signaling;

#[derive(Serialize, Deserialize)]
pub struct LoadPlyResponse {
    pub success: bool,
    pub point_cloud: Option<PointCloud>,
    pub error: Option<String>,
    pub original_count: usize,
    pub processed_count: usize,
}

#[derive(Serialize, Deserialize)]
pub struct DownsampleRequest {
    pub voxel_size: f32,
    pub target_points: Option<usize>,
}

#[derive(Serialize, Deserialize)]
pub struct DownsampleResponse {
    pub success: bool,
    pub point_cloud: Option<PointCloud>,
    pub error: Option<String>,
    pub original_count: usize,
    pub downsampled_count: usize,
}

#[derive(Serialize, Deserialize)]
pub struct CompressResponse {
    pub success: bool,
    pub data: Option<Vec<u8>>,
    pub error: Option<String>,
    pub original_size: usize,
    pub compressed_size: usize,
    pub ratio: f64,
}

#[derive(Serialize, Deserialize)]
pub struct DecompressResponse {
    pub success: bool,
    pub point_cloud: Option<PointCloud>,
    pub error: Option<String>,
}

#[derive(Serialize, Deserialize)]
pub struct StartServerRequest {
    pub port: u16,
}

#[derive(Serialize, Deserialize)]
pub struct ServerInfo {
    pub running: bool,
    pub port: u16,
    pub connections: Vec<ConnectionInfo>,
}

#[derive(Serialize, Deserialize)]
pub struct TransferInfo {
    pub id: String,
    pub from: String,
    pub to: String,
    pub progress: f64,
    pub speed: f64,
    pub transferred: usize,
    pub total: usize,
}

#[derive(Serialize, Deserialize)]
pub struct BackpressureConfig {
    pub channel_id: String,
    pub low_watermark_mb: f64,
    pub high_watermark_mb: f64,
    pub chunk_size_kb: usize,
}

#[derive(Serialize, Deserialize)]
pub struct BufferStatus {
    pub low_watermark: usize,
    pub high_watermark: usize,
    pub is_paused: bool,
}

#[tauri::command]
pub async fn configure_backpressure_settings(
    config: BackpressureConfig,
    state: State<'_, AppState>,
) -> Result<(), String> {
    configure_backpressure(
        &state,
        &config.channel_id,
        config.low_watermark_mb,
        config.high_watermark_mb,
        config.chunk_size_kb,
    );
    Ok(())
}

#[tauri::command]
pub async fn pause_transfer(
    channel_id: String,
    state: State<'_, AppState>,
) -> Result<(), String> {
    pause_channel(&state, &channel_id);
    Ok(())
}

#[tauri::command]
pub async fn resume_transfer(
    channel_id: String,
    state: State<'_, AppState>,
) -> Result<(), String> {
    resume_channel(&state, &channel_id);
    Ok(())
}

#[tauri::command]
pub async fn get_channel_buffer_status(
    channel_id: String,
    state: State<'_, AppState>,
) -> Result<Option<BufferStatus>, String> {
    Ok(get_buffer_status(&state, &channel_id).map(|(low, high, paused)| BufferStatus {
        low_watermark: low,
        high_watermark: high,
        is_paused: paused,
    }))
}

#[derive(Serialize, Deserialize)]
pub struct LoadTargetPlyResponse {
    pub success: bool,
    pub point_cloud: Option<PointCloud>,
    pub error: Option<String>,
    pub point_count: usize,
}

#[tauri::command]
pub async fn load_target_ply_file(
    path: String,
    state: State<'_, AppState>,
) -> Result<LoadTargetPlyResponse, String> {
    let path = PathBuf::from(&path);

    if !path.exists() {
        return Ok(LoadTargetPlyResponse {
            success: false,
            point_cloud: None,
            error: Some(format!("File not found: {}", path.display())),
            point_count: 0,
        });
    }

    match load_ply(&path) {
        Ok(mut cloud) => {
            let point_count = cloud.len();
            cloud.translate_to_origin();
            cloud.normalize_scale();

            {
                let mut data = state.lock();
                data.target_point_cloud = Some(cloud.clone());
            }

            Ok(LoadTargetPlyResponse {
                success: true,
                point_cloud: Some(cloud),
                error: None,
                point_count,
            })
        }
        Err(e) => Ok(LoadTargetPlyResponse {
            success: false,
            point_cloud: None,
            error: Some(format!("Failed to load PLY: {}", e)),
            point_count: 0,
        }),
    }
}

#[tauri::command]
pub async fn get_target_point_cloud(
    state: State<'_, AppState>,
) -> Result<Option<PointCloud>, String> {
    Ok(state.lock().target_point_cloud.clone())
}

#[derive(Serialize, Deserialize)]
pub struct ICPRegistrationRequest {
    pub max_iterations: Option<usize>,
    pub tolerance: Option<f64>,
    pub max_correspondence_distance: Option<f64>,
    pub use_robust_kernel: Option<bool>,
    pub robust_kernel_threshold: Option<f64>,
}

#[tauri::command]
pub async fn run_icp_registration(
    request: ICPRegistrationRequest,
    state: State<'_, AppState>,
) -> Result<Option<RegistrationResult>, String> {
    let data = state.lock();
    let source = data.point_cloud.clone();
    let target = data.target_point_cloud.clone();
    drop(data);

    let (source, target) = match (source, target) {
        (Some(s), Some(t)) => (s, t),
        _ => return Ok(None),
    };

    let config = ICPConfig {
        max_iterations: request.max_iterations.unwrap_or(100),
        tolerance: request.tolerance.unwrap_or(1e-8),
        max_correspondence_distance: request.max_correspondence_distance.unwrap_or(f64::MAX),
        use_robust_kernel: request.use_robust_kernel.unwrap_or(true),
        robust_kernel_threshold: request.robust_kernel_threshold.unwrap_or(0.5),
        initial_transformation: None,
    };

    let result = point_to_point_icp(&source, &target, &config);

    {
        let mut data = state.lock();
        data.registration_result = Some(result.clone());
    }

    Ok(Some(result))
}

#[derive(Serialize, Deserialize)]
pub struct ManualCorrespondence {
    pub source_point: [f32; 3],
    pub target_point: [f32; 3],
}

#[tauri::command]
pub async fn run_manual_registration(
    correspondences: Vec<ManualCorrespondence>,
    state: State<'_, AppState>,
) -> Result<Option<RegistrationResult>, String> {
    if correspondences.len() < 3 {
        return Err("At least 3 correspondences required".to_string());
    }

    let data = state.lock();
    let source = data.point_cloud.clone();
    let target = data.target_point_cloud.clone();
    drop(data);

    let (source, target) = match (source, target) {
        (Some(s), Some(t)) => (s, t),
        _ => return Ok(None),
    };

    let icp_correspondences: Vec<Correspondence> = correspondences
        .iter()
        .enumerate()
        .map(|(i, c)| Correspondence {
            source_index: i,
            target_index: i,
            source_point: Point::new(c.source_point[0], c.source_point[1], c.source_point[2]),
            target_point: Point::new(c.target_point[0], c.target_point[1], c.target_point[2]),
        })
        .collect();

    match manual_registration(&source, &target, &icp_correspondences) {
        Some(result) => {
            let mut data = state.lock();
            data.registration_result = Some(result.clone());
            Ok(Some(result))
        }
        None => Ok(None),
    }
}

#[tauri::command]
pub async fn get_registration_result(
    state: State<'_, AppState>,
) -> Result<Option<RegistrationResult>, String> {
    Ok(state.lock().registration_result.clone())
}

#[tauri::command]
pub async fn get_merged_point_cloud(
    state: State<'_, AppState>,
) -> Result<Option<PointCloud>, String> {
    let data = state.lock();
    let source = data.point_cloud.clone();
    let target = data.target_point_cloud.clone();
    let registration = data.registration_result.clone();
    drop(data);

    match (source, target, registration) {
        (Some(s), Some(t), Some(r)) => {
            let aligned_source = PointCloud::new(
                r.aligned_points,
                s.colors.clone(),
                s.normals.clone(),
            );
            Ok(Some(merge_point_clouds(&aligned_source, &t)))
        }
        (Some(s), Some(t), None) => {
            Ok(Some(merge_point_clouds(&s, &t)))
        }
        _ => Ok(None),
    }
}

#[tauri::command]
pub async fn get_aligned_source_point_cloud(
    state: State<'_, AppState>,
) -> Result<Option<PointCloud>, String> {
    let data = state.lock();
    let source = data.point_cloud.clone();
    let registration = data.registration_result.clone();
    drop(data);

    match (source, registration) {
        (Some(s), Some(r)) => Ok(Some(PointCloud::new(
            r.aligned_points,
            s.colors.clone(),
            s.normals.clone(),
        ))),
        _ => Ok(None),
    }
}

#[tauri::command]
pub async fn clear_registration(
    state: State<'_, AppState>,
) -> Result<(), String> {
    let mut data = state.lock();
    data.registration_result = None;
    Ok(())
}

#[tauri::command]
pub async fn clear_target_point_cloud(
    state: State<'_, AppState>,
) -> Result<(), String> {
    let mut data = state.lock();
    data.target_point_cloud = None;
    data.registration_result = None;
    Ok(())
}

#[tauri::command]
pub async fn load_ply_file(
    path: String,
    state: State<'_, AppState>,
) -> Result<LoadPlyResponse, String> {
    let path = PathBuf::from(&path);

    if !path.exists() {
        return Ok(LoadPlyResponse {
            success: false,
            point_cloud: None,
            error: Some(format!("File not found: {}", path.display())),
            original_count: 0,
            processed_count: 0,
        });
    }

    match load_ply(&path) {
        Ok(mut cloud) => {
            let original_count = cloud.len();
            cloud.translate_to_origin();
            cloud.normalize_scale();

            {
                let mut data = state.lock();
                data.point_cloud = Some(cloud.clone());
            }

            Ok(LoadPlyResponse {
                success: true,
                point_cloud: Some(cloud),
                error: None,
                original_count,
                processed_count: original_count,
            })
        }
        Err(e) => Ok(LoadPlyResponse {
            success: false,
            point_cloud: None,
            error: Some(format!("Failed to load PLY: {}", e)),
            original_count: 0,
            processed_count: 0,
        }),
    }
}

#[tauri::command]
pub async fn downsample_point_cloud(
    request: DownsampleRequest,
    state: State<'_, AppState>,
) -> Result<DownsampleResponse, String> {
    let cloud = {
        let data = state.lock();
        data.point_cloud.clone()
    };

    let cloud = match cloud {
        Some(c) => c,
        None => {
            return Ok(DownsampleResponse {
                success: false,
                point_cloud: None,
                error: Some("No point cloud loaded".to_string()),
                original_count: 0,
                downsampled_count: 0,
            });
        }
    };

    let original_count = cloud.len();
    let downsampled = if let Some(target) = request.target_points {
        adaptive_downsample(&cloud, target)
    } else {
        voxel_grid_downsample(&cloud, request.voxel_size)
    };

    let downsampled_count = downsampled.len();

    {
        let mut data = state.lock();
        data.point_cloud = Some(downsampled.clone());
    }

    Ok(DownsampleResponse {
        success: true,
        point_cloud: Some(downsampled),
        error: None,
        original_count,
        downsampled_count,
    })
}

#[tauri::command]
pub async fn get_point_cloud(
    state: State<'_, AppState>,
) -> Result<Option<PointCloud>, String> {
    Ok(state.lock().point_cloud.clone())
}

#[tauri::command]
pub async fn compress_current_point_cloud(
    state: State<'_, AppState>,
) -> Result<CompressResponse, String> {
    let cloud = {
        let data = state.lock();
        data.point_cloud.clone()
    };

    let cloud = match cloud {
        Some(c) => c,
        None => {
            return Ok(CompressResponse {
                success: false,
                data: None,
                error: Some("No point cloud loaded".to_string()),
                original_size: 0,
                compressed_size: 0,
                ratio: 0.0,
            });
        }
    };

    match compress_point_cloud(&cloud) {
        Ok(compressed) => match serialize_compressed(&compressed) {
            Ok(data) => {
                let original_size = compressed.header.original_size;
                let compressed_size = data.len();
                let ratio = if original_size > 0 {
                    compressed_size as f64 / original_size as f64
                } else {
                    0.0
                };

                Ok(CompressResponse {
                    success: true,
                    data: Some(data),
                    error: None,
                    original_size,
                    compressed_size,
                    ratio,
                })
            }
            Err(e) => Ok(CompressResponse {
                success: false,
                data: None,
                error: Some(format!("Serialization failed: {}", e)),
                original_size: 0,
                compressed_size: 0,
                ratio: 0.0,
            }),
        },
        Err(e) => Ok(CompressResponse {
            success: false,
            data: None,
            error: Some(format!("Compression failed: {}", e)),
            original_size: 0,
            compressed_size: 0,
            ratio: 0.0,
        }),
    }
}

#[tauri::command]
pub async fn decompress_point_cloud_data(
    data: Vec<u8>,
    state: State<'_, AppState>,
) -> Result<DecompressResponse, String> {
    match deserialize_compressed(&data) {
        Ok(compressed) => match decompress_point_cloud(&compressed) {
            Ok(cloud) => {
                {
                    let mut data = state.lock();
                    data.point_cloud = Some(cloud.clone());
                }

                Ok(DecompressResponse {
                    success: true,
                    point_cloud: Some(cloud),
                    error: None,
                })
            }
            Err(e) => Ok(DecompressResponse {
                success: false,
                point_cloud: None,
                error: Some(format!("Decompression failed: {}", e)),
            }),
        },
        Err(e) => Ok(DecompressResponse {
            success: false,
            point_cloud: None,
            error: Some(format!("Deserialization failed: {}", e)),
        }),
    }
}

#[tauri::command]
pub async fn start_signaling_server(
    request: StartServerRequest,
    state: State<'_, AppState>,
    app_handle: tauri::AppHandle,
) -> Result<ServerInfo, String> {
    let state_clone = state.inner().clone();
    let port = request.port;

    tokio::spawn(async move {
        let server = webrtc_signaling::SignalingServer::new(state_clone, port);
        if let Err(e) = server.start().await {
            log::error!("Signaling server error: {}", e);
        }
    });

    Ok(ServerInfo {
        running: true,
        port,
        connections: Vec::new(),
    })
}

#[tauri::command]
pub async fn get_server_info(
    state: State<'_, AppState>,
) -> Result<ServerInfo, String> {
    let data = state.lock();
    Ok(ServerInfo {
        running: true,
        port: 0,
        connections: data.connections.values().cloned().collect(),
    })
}

#[tauri::command]
pub async fn get_transfer_progress(
    state: State<'_, AppState>,
) -> Result<Vec<TransferProgress>, String> {
    Ok(webrtc_signaling::get_transfer_progress(&state))
}

#[tauri::command]
pub async fn get_transfer_stats(
    state: State<'_, AppState>,
) -> Result<TransferStats, String> {
    Ok(webrtc_signaling::get_stats(&state))
}

#[tauri::command]
pub async fn apply_height_coloring(
    state: State<'_, AppState>,
) -> Result<Option<PointCloud>, String> {
    let mut data = state.lock();
    if let Some(ref mut cloud) = data.point_cloud {
        cloud.apply_height_coloring();
        Ok(Some(cloud.clone()))
    } else {
        Ok(None)
    }
}

#[tauri::command]
pub async fn get_voxel_size_for_target(
    target_points: usize,
    state: State<'_, AppState>,
) -> Result<f32, String> {
    let data = state.lock();
    if let Some(ref cloud) = data.point_cloud {
        Ok(compute_voxel_size_for_target(cloud, target_points))
    } else {
        Ok(0.0)
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let state = Arc::new(Mutex::new(AppData::default()));

    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .manage(state)
        .invoke_handler(tauri::generate_handler![
            load_ply_file,
            downsample_point_cloud,
            get_point_cloud,
            compress_current_point_cloud,
            decompress_point_cloud_data,
            start_signaling_server,
            get_server_info,
            get_transfer_progress,
            get_transfer_stats,
            apply_height_coloring,
            get_voxel_size_for_target,
            configure_backpressure_settings,
            pause_transfer,
            resume_transfer,
            get_channel_buffer_status,
            load_target_ply_file,
            get_target_point_cloud,
            run_icp_registration,
            run_manual_registration,
            get_registration_result,
            get_merged_point_cloud,
            get_aligned_source_point_cloud,
            clear_registration,
            clear_target_point_cloud,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
