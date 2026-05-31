import { invoke } from "@tauri-apps/api/core";
import type {
  PointCloud,
  LoadPlyResponse,
  DownsampleRequest,
  DownsampleResponse,
  CompressResponse,
  DecompressResponse,
  ServerInfo,
  TransferProgress,
  TransferStats,
  StartServerRequest,
  BackpressureConfig,
  BufferStatus,
  RegistrationResult,
  ManualCorrespondence,
  ICPRegistrationRequest,
  LoadTargetPlyResponse,
} from "./types";

export async function loadPlyFile(path: string): Promise<LoadPlyResponse> {
  return invoke("load_ply_file", { path });
}

export async function downsamplePointCloud(
  request: DownsampleRequest
): Promise<DownsampleResponse> {
  return invoke("downsample_point_cloud", { request });
}

export async function getPointCloud(): Promise<PointCloud | null> {
  return invoke("get_point_cloud");
}

export async function compressCurrentPointCloud(): Promise<CompressResponse> {
  return invoke("compress_current_point_cloud");
}

export async function decompressPointCloudData(
  data: number[]
): Promise<DecompressResponse> {
  return invoke("decompress_point_cloud_data", { data });
}

export async function startSignalingServer(
  request: StartServerRequest
): Promise<ServerInfo> {
  return invoke("start_signaling_server", { request });
}

export async function getServerInfo(): Promise<ServerInfo> {
  return invoke("get_server_info");
}

export async function getTransferProgress(): Promise<TransferProgress[]> {
  return invoke("get_transfer_progress");
}

export async function getTransferStats(): Promise<TransferStats> {
  return invoke("get_transfer_stats");
}

export async function applyHeightColoring(): Promise<PointCloud | null> {
  return invoke("apply_height_coloring");
}

export async function getVoxelSizeForTarget(
  targetPoints: number
): Promise<number> {
  return invoke("get_voxel_size_for_target", { targetPoints });
}

export async function configureBackpressure(
  config: BackpressureConfig
): Promise<void> {
  return invoke("configure_backpressure_settings", { config });
}

export async function pauseTransfer(channelId: string): Promise<void> {
  return invoke("pause_transfer", { channelId });
}

export async function resumeTransfer(channelId: string): Promise<void> {
  return invoke("resume_transfer", { channelId });
}

export async function getChannelBufferStatus(
  channelId: string
): Promise<BufferStatus | null> {
  return invoke("get_channel_buffer_status", { channelId });
}

export async function loadTargetPlyFile(
  path: string
): Promise<LoadTargetPlyResponse> {
  return invoke("load_target_ply_file", { path });
}

export async function getTargetPointCloud(): Promise<PointCloud | null> {
  return invoke("get_target_point_cloud");
}

export async function runICPRegistration(
  request: ICPRegistrationRequest
): Promise<RegistrationResult | null> {
  return invoke("run_icp_registration", { request });
}

export async function runManualRegistration(
  correspondences: ManualCorrespondence[]
): Promise<RegistrationResult | null> {
  return invoke("run_manual_registration", { correspondences });
}

export async function getRegistrationResult(): Promise<RegistrationResult | null> {
  return invoke("get_registration_result");
}

export async function getMergedPointCloud(): Promise<PointCloud | null> {
  return invoke("get_merged_point_cloud");
}

export async function getAlignedSourcePointCloud(): Promise<PointCloud | null> {
  return invoke("get_aligned_source_point_cloud");
}

export async function clearRegistration(): Promise<void> {
  return invoke("clear_registration");
}

export async function clearTargetPointCloud(): Promise<void> {
  return invoke("clear_target_point_cloud");
}
