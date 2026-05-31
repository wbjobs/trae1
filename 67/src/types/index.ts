export interface ProcessingParams {
  toneMap: 'reinhard' | 'filmic' | 'aces';
  exposure: number;
  contrast: number;
  saturation: number;
  highlights: number;
  shadows: number;
  temperature: number;
  tint: number;
}

export interface ProcessedImage {
  width: number;
  height: number;
  data: Uint8Array;
}

export interface ImageFile {
  id: string;
  name: string;
  file: File;
  rawData: Uint8Array | null;
  processedImage: ProcessedImage | null;
  status: 'pending' | 'processing' | 'completed' | 'error';
  progress: number;
  error: string | null;
  tilesProcessed?: number;
  totalTiles?: number;
  tileSize?: number;
}

export interface HistogramData {
  r: number[];
  g: number[];
  b: number[];
  luminance: number[];
}

export interface MemoryEstimate {
  total_mb: number;
  hdr_mb: number;
  output_mb: number;
  warning_level: 0 | 1 | 2;
  suggested_tile_size: number;
  message: string;
}

export interface ProcessingStats {
  total_tiles: number;
  processed_tiles: number;
  tile_size: number;
  memory_used_mb: number;
  estimated_memory_mb: number;
}

export interface ImageAnalysis {
  avg_luminance: number;
  dynamic_range: number;
  r_dominance: number;
  g_dominance: number;
  b_dominance: number;
  highlight_ratio: number;
  shadow_ratio: number;
  histogram_peak: number;
  histogram_spread: number;
  color_saturation: number;
  contrast_ratio: number;
}

export interface AutoOptimizeResult {
  params: ProcessingParams;
  analysis: ImageAnalysis;
  confidence: number;
  recommendation: string;
}

export interface StylePreset {
  name: string;
  description: string;
  params: ProcessingParams;
}

export interface CustomStyleConfig {
  name: string;
  description: string;
  base_style: string;
  params: ProcessingParams;
  created_at: number;
}

export interface StyleTransferResult {
  success: boolean;
  params?: ProcessingParams;
  style_name: string;
  error?: string;
}

export interface TrainingConfig {
  styleName: string;
  description: string;
  baseStyle: string;
  epochs: number;
  learningRate: number;
}

export interface TrainingProgress {
  epoch: number;
  totalEpochs: number;
  loss: number;
  accuracy: number;
  status: 'idle' | 'training' | 'completed' | 'error';
  message?: string;
}

export interface WasmModule {
  process_raw_file: (
    fileData: Uint8Array,
    params: any
  ) => Promise<any>;
  process_raw_file_with_tile_size: (
    fileData: Uint8Array,
    params: any,
    tileSize: number
  ) => Promise<any>;
  get_histogram: (
    imageData: Float32Array,
    width: number,
    height: number,
    bins: number
  ) => Promise<HistogramData>;
  export_image: (
    processedData: Uint8Array,
    width: number,
    height: number,
    format: string,
    quality: number
  ) => Promise<Uint8Array>;
  estimate_memory_usage: (
    width: number,
    height: number
  ) => Promise<MemoryEstimate>;
  estimate_raw_memory_usage: (
    fileData: Uint8Array
  ) => Promise<MemoryEstimate>;
  get_memory_stats: () => Promise<{
    current_mb: number;
    peak_mb: number;
    allocations: number;
  }>;
  analyze_image: (
    fileData: Uint8Array
  ) => Promise<ImageAnalysis>;
  auto_optimize: (
    fileData: Uint8Array
  ) => Promise<AutoOptimizeResult>;
  apply_style_preset: (
    fileData: Uint8Array,
    styleName: string
  ) => Promise<any>;
  get_style_presets: () => Promise<Record<string, ProcessingParams>>;
  get_style_descriptions: () => Promise<[string, string][]>;
  transfer_style: (
    fileData: Uint8Array,
    styleParams: ProcessingParams,
    strength: number
  ) => Promise<any>;
}
