import { WasmModule, MemoryEstimate, ImageAnalysis, AutoOptimizeResult, ProcessingParams } from '../types';
import { analyzeImageData, autoOptimize } from '../services/aiService';

let wasmModule: WasmModule | null = null;

export async function loadWasmModule(): Promise<WasmModule> {
  if (wasmModule) {
    return wasmModule;
  }

  try {
    const realWasm = await import('hdr-wasm/pkg/hdr_wasm.js');
    wasmModule = {
      process_raw_file: async (fileData: Uint8Array, params: any) => {
        return await realWasm.process_raw_file(fileData, params);
      },
      process_raw_file_with_tile_size: async (
        fileData: Uint8Array,
        params: any,
        tileSize: number
      ) => {
        return await realWasm.process_raw_file_with_tile_size(
          fileData,
          params,
          tileSize
        );
      },
      get_histogram: async (
        imageData: Float32Array,
        width: number,
        height: number,
        bins: number
      ) => {
        return await realWasm.get_histogram(imageData, width, height, bins);
      },
      export_image: async (
        processedData: Uint8Array,
        width: number,
        height: number,
        format: string,
        quality: number
      ) => {
        return await realWasm.export_image(
          processedData,
          width,
          height,
          format,
          quality
        );
      },
      estimate_memory_usage: async (width: number, height: number) => {
        return await realWasm.estimate_memory_usage(width, height);
      },
      estimate_raw_memory_usage: async (fileData: Uint8Array) => {
        return await realWasm.estimate_raw_memory_usage(fileData);
      },
      get_memory_stats: async () => {
        return await realWasm.get_memory_stats();
      },
      analyze_image: async (fileData: Uint8Array): Promise<ImageAnalysis> => {
        try {
          return await realWasm.analyze_image(fileData);
        } catch {
          return {
            avg_luminance: 0.5,
            dynamic_range: 0.5,
            r_dominance: 1.0,
            g_dominance: 1.0,
            b_dominance: 1.0,
            highlight_ratio: 0.1,
            shadow_ratio: 0.2,
            histogram_peak: 0.5,
            histogram_spread: 0.4,
            color_saturation: 0.5,
            contrast_ratio: 0.5,
          };
        }
      },
      auto_optimize: async (fileData: Uint8Array): Promise<AutoOptimizeResult> => {
        try {
          return await realWasm.auto_optimize(fileData);
        } catch {
          const defaultAnalysis: ImageAnalysis = {
            avg_luminance: 0.5,
            dynamic_range: 0.5,
            r_dominance: 1.0,
            g_dominance: 1.0,
            b_dominance: 1.0,
            highlight_ratio: 0.1,
            shadow_ratio: 0.2,
            histogram_peak: 0.5,
            histogram_spread: 0.4,
            color_saturation: 0.5,
            contrast_ratio: 0.5,
          };
          return autoOptimize(defaultAnalysis);
        }
      },
      apply_style_preset: async (fileData: Uint8Array, styleName: string) => {
        try {
          return await realWasm.apply_style_preset(fileData, styleName);
        } catch {
          return null;
        }
      },
      get_style_presets: async (): Promise<Record<string, ProcessingParams>> => {
        try {
          return await realWasm.get_style_presets();
        } catch {
          return {};
        }
      },
      get_style_descriptions: async (): Promise<[string, string][]> => {
        try {
          return await realWasm.get_style_descriptions();
        } catch {
          return [];
        }
      },
      transfer_style: async (
        fileData: Uint8Array,
        styleParams: ProcessingParams,
        strength: number
      ) => {
        try {
          return await realWasm.transfer_style(fileData, styleParams, strength);
        } catch {
          return null;
        }
      },
    };
  } catch (error) {
    console.log('使用模拟WASM模块:', error);
    wasmModule = createMockWasmModule();
  }

  return wasmModule;
}

function createMockWasmModule(): WasmModule {
  return {
    process_raw_file: async (fileData: Uint8Array, params: any) => {
      console.log('模拟处理RAW文件，参数:', params);

      const width = 1920;
      const height = 1280;
      const data = new Uint8Array(width * height * 4);

      for (let i = 0; i < height; i++) {
        for (let j = 0; j < width; j++) {
          const idx = (i * width + j) * 4;

          const x = j / width;
          const y = i / height;

          data[idx] = Math.floor(128 + 127 * Math.sin(x * Math.PI * 2) * Math.cos(y * Math.PI));
          data[idx + 1] = Math.floor(128 + 127 * Math.sin(y * Math.PI * 2) * Math.sin(x * Math.PI));
          data[idx + 2] = Math.floor(128 + 127 * Math.cos(x * Math.PI * 2) * Math.cos(y * Math.PI * 2));
          data[idx + 3] = 255;
        }
      }

      return {
        width,
        height,
        data: Array.from(data),
      };
    },

    process_raw_file_with_tile_size: async (
      fileData: Uint8Array,
      params: any,
      tileSize: number
    ) => {
      console.log('模拟处理RAW文件（分块），瓦片大小:', tileSize, '参数:', params);

      const width = 1920;
      const height = 1280;
      const data = new Uint8Array(width * height * 4);

      for (let tileY = 0; tileY < Math.ceil(height / tileSize); tileY++) {
        for (let tileX = 0; tileX < Math.ceil(width / tileSize); tileX++) {
          const startX = tileX * tileSize;
          const startY = tileY * tileSize;
          const endX = Math.min(startX + tileSize, width);
          const endY = Math.min(startY + tileSize, height);

          for (let i = startY; i < endY; i++) {
            for (let j = startX; j < endX; j++) {
              const idx = (i * width + j) * 4;

              const x = j / width;
              const y = i / height;

              data[idx] = Math.floor(128 + 127 * Math.sin(x * Math.PI * 2) * Math.cos(y * Math.PI));
              data[idx + 1] = Math.floor(128 + 127 * Math.sin(y * Math.PI * 2) * Math.sin(x * Math.PI));
              data[idx + 2] = Math.floor(128 + 127 * Math.cos(x * Math.PI * 2) * Math.cos(y * Math.PI * 2));
              data[idx + 3] = 255;
            }
          }
        }
      }

      return {
        width,
        height,
        data: Array.from(data),
      };
    },

    get_histogram: async (
      imageData: Float32Array,
      width: number,
      height: number,
      bins: number
    ) => {
      const r = new Array(bins).fill(0);
      const g = new Array(bins).fill(0);
      const b = new Array(bins).fill(0);
      const luminance = new Array(bins).fill(0);

      const sampleStep = Math.max(1, Math.floor((width * height) / 10000));

      for (let i = 0; i < width * height; i += sampleStep) {
        const idx = i * 3;
        if (idx + 2 >= imageData.length) break;

        const rv = imageData[idx];
        const gv = imageData[idx + 1];
        const bv = imageData[idx + 2];

        const lum = 0.299 * rv + 0.587 * gv + 0.114 * bv;

        const rBin = Math.min(bins - 1, Math.floor(rv * bins));
        const gBin = Math.min(bins - 1, Math.floor(gv * bins));
        const bBin = Math.min(bins - 1, Math.floor(bv * bins));
        const lBin = Math.min(bins - 1, Math.floor(lum * bins));

        r[rBin]++;
        g[gBin]++;
        b[bBin]++;
        luminance[lBin]++;
      }

      return { r, g, b, luminance };
    },

    export_image: async (
      processedData: Uint8Array,
      width: number,
      height: number,
      format: string,
      quality: number
    ) => {
      console.log(`模拟导出图像: ${width}x${height}, 格式: ${format}, 质量: ${quality}`);

      const header = new Uint8Array([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
      return new Uint8Array([...header, ...processedData.slice(0, 100)]);
    },

    estimate_memory_usage: async (width: number, height: number) => {
      const totalPixels = width * height;
      const hdrMemory = totalPixels * 3 * 4;
      const outputMemory = totalPixels * 4;
      const tileMemory = 512 * 512 * 3 * 4 + 512 * 512 * 4;
      const overhead = 50 * 1024 * 1024;

      const total = hdrMemory + outputMemory + tileMemory + overhead;
      const totalMb = total / (1024 * 1024);

      let warningLevel: 0 | 1 | 2 = 0;
      let suggestedTileSize = 512;
      let message = '';

      if (totalMb > 2048) {
        warningLevel = 2;
        suggestedTileSize = 128;
        message = `内存使用警告：预计需要 ${totalMb.toFixed(1)}MB，建议使用128x128瓦片或降低分辨率处理`;
      } else if (totalMb > 1024) {
        warningLevel = 1;
        suggestedTileSize = 256;
        message = `内存使用较高：预计需要 ${totalMb.toFixed(1)}MB，建议使用256x256瓦片`;
      }

      return {
        total_mb: totalMb,
        hdr_mb: hdrMemory / (1024 * 1024),
        output_mb: outputMemory / (1024 * 1024),
        warning_level: warningLevel,
        suggested_tile_size: suggestedTileSize,
        message,
      } as MemoryEstimate;
    },

    estimate_raw_memory_usage: async (fileData: Uint8Array) => {
      return {
        total_mb: 512,
        hdr_mb: 256,
        output_mb: 128,
        warning_level: 0,
        suggested_tile_size: 512,
        message: '',
      } as MemoryEstimate;
    },

    get_memory_stats: async () => {
      return {
        current_mb: 256,
        peak_mb: 512,
        allocations: 10,
      };
    },

    analyze_image: async (fileData: Uint8Array): Promise<ImageAnalysis> => {
      return {
        avg_luminance: 0.5,
        dynamic_range: 0.6,
        r_dominance: 1.0,
        g_dominance: 1.0,
        b_dominance: 1.0,
        highlight_ratio: 0.1,
        shadow_ratio: 0.2,
        histogram_peak: 0.5,
        histogram_spread: 0.5,
        color_saturation: 0.5,
        contrast_ratio: 0.6,
      };
    },

    auto_optimize: async (fileData: Uint8Array): Promise<AutoOptimizeResult> => {
      const analysis: ImageAnalysis = {
        avg_luminance: 0.5,
        dynamic_range: 0.6,
        r_dominance: 1.0,
        g_dominance: 1.0,
        b_dominance: 1.0,
        highlight_ratio: 0.1,
        shadow_ratio: 0.2,
        histogram_peak: 0.5,
        histogram_spread: 0.5,
        color_saturation: 0.5,
        contrast_ratio: 0.6,
      };

      return autoOptimize(analysis);
    },

    apply_style_preset: async (fileData: Uint8Array, styleName: string) => {
      console.log('模拟应用风格预设:', styleName);
      return null;
    },

    get_style_presets: async (): Promise<Record<string, ProcessingParams>> => {
      return {};
    },

    get_style_descriptions: async (): Promise<[string, string][]> => {
      return [];
    },

    transfer_style: async (
      fileData: Uint8Array,
      styleParams: ProcessingParams,
      strength: number
    ) => {
      console.log('模拟风格迁移，强度:', strength);
      return null;
    },
  };
}

export function getWasmModule(): WasmModule {
  if (!wasmModule) {
    throw new Error('WASM模块未初始化');
  }
  return wasmModule;
}
