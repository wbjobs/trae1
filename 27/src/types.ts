export interface Point {
  x: number;
  y: number;
  z: number;
}

export interface Color {
  r: number;
  g: number;
  b: number;
  a: number;
}

export interface Normal {
  nx: number;
  ny: number;
  nz: number;
}

export interface BoundingBox {
  min: Point;
  max: Point;
}

export interface PointCloud {
  points: Point[];
  colors: Color[];
  normals?: Normal[];
  bounds: BoundingBox;
}

export interface ConnectionInfo {
  id: string;
  peer_id?: string;
  role: ConnectionRole;
  status: ConnectionStatus;
  address: string;
}

export type ConnectionRole = 'Sender' | 'Receiver';
export type ConnectionStatus = 'Connected' | 'Disconnected' | 'Transferring' | 'Error';

export interface TransferProgress {
  transfer_id: string;
  total_size: number;
  transferred: number;
  start_time: number;
  last_update: number;
  bytes_per_second: number;
  buffered_amount: number;
  chunk_delay_ms: number;
}

export interface TransferStats {
  total_sent: number;
  total_received: number;
  current_speed: number;
  peak_speed: number;
  backpressure_events: number;
  chunks_sent: number;
  chunks_dropped: number;
}

export interface BackpressureConfig {
  channel_id: string;
  low_watermark_mb: number;
  high_watermark_mb: number;
  chunk_size_kb: number;
}

export interface BufferStatus {
  low_watermark: number;
  high_watermark: number;
  is_paused: boolean;
}

export interface LoadPlyResponse {
  success: boolean;
  point_cloud?: PointCloud;
  error?: string;
  original_count: number;
  processed_count: number;
}

export interface DownsampleRequest {
  voxel_size: number;
  target_points?: number;
}

export interface DownsampleResponse {
  success: boolean;
  point_cloud?: PointCloud;
  error?: string;
  original_count: number;
  downsampled_count: number;
}

export interface CompressResponse {
  success: boolean;
  data?: number[];
  error?: string;
  original_size: number;
  compressed_size: number;
  ratio: number;
}

export interface DecompressResponse {
  success: boolean;
  point_cloud?: PointCloud;
  error?: string;
}

export interface ServerInfo {
  running: boolean;
  port: number;
  connections: ConnectionInfo[];
}

export interface RegistrationResult {
  transformation: Matrix4Data;
  aligned_points: Point[];
  rmse: number;
  iterations: number;
  converged: boolean;
  fitness: number;
  inlier_rmse: number;
  correspondence_count: number;
}

export interface Matrix4Data {
  data: [[number, number, number, number], [number, number, number, number], [number, number, number, number], [number, number, number, number]];
}

export interface ManualCorrespondence {
  source_point: [number, number, number];
  target_point: [number, number, number];
}

export interface ICPRegistrationRequest {
  max_iterations?: number;
  tolerance?: number;
  max_correspondence_distance?: number;
  use_robust_kernel?: boolean;
  robust_kernel_threshold?: number;
}

export interface LoadTargetPlyResponse {
  success: boolean;
  point_cloud?: PointCloud;
  error?: string;
  point_count: number;
}
