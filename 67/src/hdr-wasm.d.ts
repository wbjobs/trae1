declare module 'hdr-wasm/pkg/hdr_wasm.js' {
  interface ProcessingParams {
    tone_map: string;
    exposure: number;
    contrast: number;
    saturation: number;
    highlights: number;
    shadows: number;
    temperature: number;
    tint: number;
  }

  interface ProcessResult {
    width: number;
    height: number;
    data: number[];
  }

  interface HistogramData {
    r: number[];
    g: number[];
    b: number[];
    luminance: number[];
  }

  interface MemoryEstimate {
    total_mb: number;
    hdr_mb: number;
    output_mb: number;
    warning_level: 0 | 1 | 2;
    suggested_tile_size: number;
    message: string;
  }

  interface ImageAnalysis {
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

  interface AutoOptimizeResult {
    params: any;
    analysis: ImageAnalysis;
    confidence: number;
    recommendation: string;
  }

  export function process_raw_file(
    fileData: Uint8Array,
    params: ProcessingParams
  ): Promise<ProcessResult>;

  export function process_raw_file_with_tile_size(
    fileData: Uint8Array,
    params: ProcessingParams,
    tileSize: number
  ): Promise<ProcessResult>;

  export function get_histogram(
    imageData: Float32Array,
    width: number,
    height: number,
    bins: number
  ): Promise<HistogramData>;

  export function export_image(
    processedData: Uint8Array,
    width: number,
    height: number,
    format: string,
    quality: number
  ): Promise<Uint8Array>;

  export function estimate_memory_usage(
    width: number,
    height: number
  ): Promise<MemoryEstimate>;

  export function estimate_raw_memory_usage(
    fileData: Uint8Array
  ): Promise<MemoryEstimate>;

  export function get_memory_stats(): Promise<{
    current_mb: number;
    peak_mb: number;
    allocations: number;
  }>;

  export function analyze_image(
    fileData: Uint8Array
  ): Promise<ImageAnalysis>;

  export function auto_optimize(
    fileData: Uint8Array
  ): Promise<AutoOptimizeResult>;

  export function apply_style_preset(
    fileData: Uint8Array,
    styleName: string
  ): Promise<any>;

  export function get_style_presets(): Promise<Record<string, any>>;

  export function get_style_descriptions(): Promise<[string, string][]>;

  export function transfer_style(
    fileData: Uint8Array,
    styleParams: any,
    strength: number
  ): Promise<any>;
}
