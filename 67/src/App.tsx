import React, { useState, useEffect, useCallback, useMemo } from 'react';
import { ImageFile, ProcessingParams, ProcessedImage, HistogramData, MemoryEstimate, AutoOptimizeResult, CustomStyleConfig } from './types';
import { loadWasmModule } from './wasm/wasmLoader';
import { FileUploader } from './components/FileUploader';
import { ImageViewer } from './components/ImageViewer';
import { ControlPanel } from './components/ControlPanel';
import { Histogram } from './components/Histogram';
import { BatchQueue } from './components/BatchQueue';
import { ExportDialog } from './components/ExportDialog';
import { AIControlPanel } from './components/AIControlPanel';
import { TrainingDialog } from './components/TrainingDialog';
import { initAIService } from './services/aiService';

const MAX_BATCH_SIZE = 5;

const defaultParams: ProcessingParams = {
  toneMap: 'reinhard',
  exposure: 0,
  contrast: 1,
  saturation: 1,
  highlights: 0,
  shadows: 0,
  temperature: 0,
  tint: 0,
};

const App: React.FC = () => {
  const [wasmReady, setWasmReady] = useState(false);
  const [wasmError, setWasmError] = useState<string | null>(null);
  const [files, setFiles] = useState<ImageFile[]>([]);
  const [currentIndex, setCurrentIndex] = useState(0);
  const [params, setParams] = useState<ProcessingParams>(defaultParams);
  const [processedImage, setProcessedImage] = useState<ProcessedImage | null>(null);
  const [originalPreview, setOriginalPreview] = useState<ProcessedImage | null>(null);
  const [histogram, setHistogram] = useState<HistogramData | null>(null);
  const [originalHistogram, setOriginalHistogram] = useState<HistogramData | null>(null);
  const [showComparison, setShowComparison] = useState(false);
  const [comparisonPosition, setComparisonPosition] = useState(50);
  const [showExportDialog, setShowExportDialog] = useState(false);
  const [isProcessing, setIsProcessing] = useState(false);
  const [memoryWarning, setMemoryWarning] = useState<MemoryEstimate | null>(null);
  const [tileSize, setTileSize] = useState<number>(0);
  const [showTrainingDialog, setShowTrainingDialog] = useState(false);
  const [customStylesUpdated, setCustomStylesUpdated] = useState(false);
  const [rawImageData, setRawImageData] = useState<Float32Array | null>(null);
  const [rawImageWidth, setRawImageWidth] = useState(0);
  const [rawImageHeight, setRawImageHeight] = useState(0);

  useEffect(() => {
    const init = async () => {
      try {
        await Promise.all([loadWasmModule(), initAIService()]);
        setWasmReady(true);
      } catch (error) {
        setWasmError((error as Error).message);
      }
    };
    init();
  }, []);

  const readFile = (file: File): Promise<Uint8Array> => {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => {
        resolve(new Uint8Array(reader.result as ArrayBuffer));
      };
      reader.onerror = reject;
      reader.readAsArrayBuffer(file);
    });
  };

  const estimateMemory = useCallback(async (rawData: Uint8Array) => {
    try {
      const wasm = await loadWasmModule();
      const estimate = await wasm.estimate_raw_memory_usage(rawData);
      setMemoryWarning(estimate);

      if (estimate.warning_level > 0) {
        setTileSize(estimate.suggested_tile_size);
      }

      return estimate;
    } catch (error) {
      console.error('内存预估失败:', error);
      return null;
    }
  }, []);

  const processFile = useCallback(async (file: ImageFile, currentParams: ProcessingParams) => {
    try {
      const wasm = await loadWasmModule();

      if (!file.rawData) {
        return null;
      }

      const paramsForWasm = {
        tone_map: currentParams.toneMap,
        exposure: currentParams.exposure,
        contrast: currentParams.contrast,
        saturation: currentParams.saturation,
        highlights: currentParams.highlights,
        shadows: currentParams.shadows,
        temperature: currentParams.temperature,
        tint: currentParams.tint,
      };

      let result: any;

      if (tileSize > 0) {
        result = await wasm.process_raw_file_with_tile_size(
          file.rawData,
          paramsForWasm,
          tileSize
        );
      } else {
        result = await wasm.process_raw_file(file.rawData, paramsForWasm);
      }

      if (result) {
        return {
          width: result.width,
          height: result.height,
          data: new Uint8Array(result.data),
        } as ProcessedImage;
      }

      return null;
    } catch (error) {
      console.error('处理文件时出错:', error);
      return null;
    }
  }, [tileSize]);

  const handleFilesSelected = useCallback((newFiles: ImageFile[]) => {
    const availableSlots = MAX_BATCH_SIZE - files.length;

    if (newFiles.length > availableSlots) {
      newFiles = newFiles.slice(0, availableSlots);
    }

    if (newFiles.length === 0) {
      return;
    }

    setFiles((prev) => [...prev, ...newFiles]);

    newFiles.forEach(async (file, index) => {
      const actualIndex = files.length + index;

      try {
        const rawData = await readFile(file.file);

        await estimateMemory(rawData);

        setFiles((prev) =>
          prev.map((f, i) =>
            i === actualIndex
              ? { ...f, rawData, status: 'pending' as const, progress: 0 }
              : f
          )
        );

        if (actualIndex === currentIndex && processedImage === null) {
          setIsProcessing(true);
          setFiles((prev) =>
            prev.map((f, i) =>
              i === actualIndex ? { ...f, status: 'processing' as const, progress: 10 } : f
            )
          );

          const result = await processFile(
            { ...file, rawData },
            params
          );

          if (result) {
            setProcessedImage(result);

            const floatData = new Float32Array(result.width * result.height * 3);
            for (let i = 0; i < result.width * result.height; i++) {
              const srcIdx = i * 4;
              const dstIdx = i * 3;
              floatData[dstIdx] = result.data[srcIdx] / 255;
              floatData[dstIdx + 1] = result.data[srcIdx + 1] / 255;
              floatData[dstIdx + 2] = result.data[srcIdx + 2] / 255;
            }
            setRawImageData(floatData);
            setRawImageWidth(result.width);
            setRawImageHeight(result.height);

            setFiles((prev) =>
              prev.map((f, i) =>
                i === actualIndex
                  ? { ...f, status: 'completed' as const, progress: 100 }
                  : f
              )
            );
          } else {
            setFiles((prev) =>
              prev.map((f, i) =>
                i === actualIndex
                  ? { ...f, status: 'error' as const, error: '处理失败' }
                  : f
              )
            );
          }

          setIsProcessing(false);
        }
      } catch (error) {
        setFiles((prev) =>
          prev.map((f, i) =>
            i === actualIndex
              ? { ...f, status: 'error' as const, error: (error as Error).message }
              : f
          )
        );
      }
    });
  }, [files.length, currentIndex, processedImage, params, processFile, estimateMemory]);

  const handleRemoveFile = useCallback((id: string) => {
    setFiles((prev) => {
      const index = prev.findIndex((f) => f.id === id);
      if (index === -1) return prev;

      const newFiles = prev.filter((f) => f.id !== id);

      if (index <= currentIndex && newFiles.length > 0) {
        const newIndex = Math.min(currentIndex, newFiles.length - 1);
        setCurrentIndex(newIndex);
      } else if (newFiles.length === 0) {
        setCurrentIndex(0);
        setProcessedImage(null);
        setRawImageData(null);
        setRawImageWidth(0);
        setRawImageHeight(0);
      }

      return newFiles;
    });
  }, [currentIndex]);

  const handleClearQueue = useCallback(() => {
    setFiles([]);
    setCurrentIndex(0);
    setProcessedImage(null);
    setOriginalPreview(null);
    setHistogram(null);
    setOriginalHistogram(null);
    setShowComparison(false);
    setMemoryWarning(null);
    setRawImageData(null);
    setRawImageWidth(0);
    setRawImageHeight(0);
  }, []);

  const handleParamsChange = useCallback(async (newParams: ProcessingParams) => {
    setParams(newParams);

    if (files.length > 0 && files[currentIndex]?.rawData) {
      setIsProcessing(true);
      setFiles((prev) =>
        prev.map((f, i) =>
          i === currentIndex ? { ...f, status: 'processing' as const, progress: 10 } : f
        )
      );

      const result = await processFile(files[currentIndex], newParams);

      if (result) {
        setProcessedImage(result);
        setFiles((prev) =>
          prev.map((f, i) =>
            i === currentIndex
              ? { ...f, status: 'completed' as const, progress: 100 }
              : f
          )
        );
      } else {
        setFiles((prev) =>
          prev.map((f, i) =>
            i === currentIndex
              ? { ...f, status: 'error' as const, error: '处理失败' }
              : f
          )
        );
      }

      setIsProcessing(false);
    }
  }, [files, currentIndex, processFile]);

  const handleExport = useCallback(async (format: string, quality: number) => {
    if (!processedImage) return;

    try {
      const wasm = await loadWasmModule();
      const exportData = await wasm.export_image(
        processedImage.data,
        processedImage.width,
        processedImage.height,
        format,
        quality
      );

      const blob = new Blob([exportData as BlobPart], { type: `image/${format}` });
      const url = URL.createObjectURL(blob);

      const link = document.createElement('a');
      link.href = url;
      link.download = `export_${Date.now()}.${format}`;
      document.body.appendChild(link);
      link.click();
      document.body.removeChild(link);

      URL.revokeObjectURL(url);
      setShowExportDialog(false);
    } catch (error) {
      console.error('导出失败:', error);
      alert('导出失败: ' + (error as Error).message);
    }
  }, [processedImage]);

  const handleAutoOptimize = useCallback((result: AutoOptimizeResult) => {
    console.log('自动优化结果:', result);
  }, []);

  const handleTrainingComplete = useCallback((style: CustomStyleConfig) => {
    setCustomStylesUpdated(prev => !prev);
    setShowTrainingDialog(false);
    setParams(style.params);
  }, []);

  const MemoryWarningBanner: React.FC = () => {
    if (!memoryWarning || memoryWarning.warning_level === 0) return null;

    const bgColor = memoryWarning.warning_level === 2 ? '#f44336' : '#ff9800';

    return (
      <div
        style={{
          backgroundColor: bgColor,
          padding: '12px 16px',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          gap: '16px',
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
          <span style={{ fontSize: '20px' }}>
            {memoryWarning.warning_level === 2 ? '⚠️' : '⚡'}
          </span>
          <div>
            <div style={{ fontSize: '14px', color: '#fff', fontWeight: 'bold' }}>
              {memoryWarning.warning_level === 2 ? '内存使用警告' : '内存使用较高'}
            </div>
            <div style={{ fontSize: '12px', color: 'rgba(255,255,255,0.9)' }}>
              {memoryWarning.message}
            </div>
            <div style={{ fontSize: '11px', color: 'rgba(255,255,255,0.7)', marginTop: '4px' }}>
              预计需要: {memoryWarning.total_mb.toFixed(1)}MB | 
              建议瓦片大小: {memoryWarning.suggested_tile_size}x{memoryWarning.suggested_tile_size}
            </div>
          </div>
        </div>

        <div style={{ display: 'flex', gap: '8px' }}>
          <select
            value={tileSize}
            onChange={(e) => setTileSize(parseInt(e.target.value))}
            style={{
              padding: '6px 12px',
              backgroundColor: 'rgba(255,255,255,0.2)',
              color: '#fff',
              border: '1px solid rgba(255,255,255,0.3)',
              borderRadius: '4px',
              fontSize: '12px',
            }}
          >
            <option value="0">自动</option>
            <option value="128">128x128</option>
            <option value="256">256x256</option>
            <option value="512">512x512</option>
            <option value="1024">1024x1024</option>
          </select>

          <button
            onClick={() => setMemoryWarning(null)}
            style={{
              padding: '6px 12px',
              backgroundColor: 'rgba(255,255,255,0.2)',
              color: '#fff',
              border: '1px solid rgba(255,255,255,0.3)',
              borderRadius: '4px',
              cursor: 'pointer',
              fontSize: '12px',
            }}
          >
            关闭
          </button>
        </div>
      </div>
    );
  };

  if (wasmError) {
    return (
      <div style={{
        width: '100%',
        height: '100%',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        backgroundColor: '#1a1a1a',
        color: '#f44336',
        fontSize: '18px',
        flexDirection: 'column',
        gap: '16px',
      }}>
        <div>WASM模块加载失败</div>
        <div style={{ fontSize: '14px', color: '#888' }}>{wasmError}</div>
        <div style={{ fontSize: '12px', color: '#666' }}>
          请确保已使用 wasm-pack 编译 Rust 代码
        </div>
      </div>
    );
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100vh' }}>
      <header
        style={{
          backgroundColor: '#2a2a2a',
          padding: '12px 24px',
          borderBottom: '1px solid #333',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
          <span style={{ fontSize: '24px' }}>🎨</span>
          <h1 style={{ fontSize: '20px', color: '#fff' }}>HDR图像处理工具</h1>
        </div>

        <div style={{ display: 'flex', gap: '12px', alignItems: 'center' }}>
          {tileSize > 0 && (
            <span style={{ fontSize: '12px', color: '#888' }}>
              瓦片: {tileSize}x{tileSize}
            </span>
          )}

          <button
            onClick={() => setShowComparison(!showComparison)}
            disabled={!processedImage || !originalPreview}
            style={{
              padding: '8px 16px',
              backgroundColor: showComparison ? '#4CAF50' : '#444',
              color: '#fff',
              border: 'none',
              borderRadius: '4px',
              cursor: processedImage && originalPreview ? 'pointer' : 'not-allowed',
              fontSize: '14px',
              opacity: processedImage && originalPreview ? 1 : 0.5,
            }}
          >
            {showComparison ? '关闭对比' : '对比视图'}
          </button>

          <button
            onClick={() => setShowExportDialog(true)}
            disabled={!processedImage}
            style={{
              padding: '8px 16px',
              backgroundColor: '#4CAF50',
              color: '#fff',
              border: 'none',
              borderRadius: '4px',
              cursor: processedImage ? 'pointer' : 'not-allowed',
              fontSize: '14px',
              opacity: processedImage ? 1 : 0.5,
            }}
          >
            导出
          </button>
        </div>
      </header>

      <MemoryWarningBanner />

      <div style={{ flex: 1, display: 'flex', overflow: 'hidden' }}>
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column' }}>
          {files.length === 0 ? (
            <div
              style={{
                flex: 1,
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                padding: '40px',
              }}
            >
              <FileUploader
                onFilesSelected={handleFilesSelected}
                isProcessing={isProcessing}
              />
            </div>
          ) : (
            <>
              <ImageViewer
                image={processedImage}
                originalPreview={originalPreview}
                showComparison={showComparison}
                comparisonPosition={comparisonPosition}
                onComparisonChange={setComparisonPosition}
              />
              <div style={{ padding: '12px 16px', backgroundColor: '#1a1a1a' }}>
                <Histogram
                  data={histogram}
                  originalData={originalHistogram}
                  showOriginal={showComparison}
                />
              </div>
            </>
          )}
        </div>

        <ControlPanel
          params={params}
          onParamsChange={handleParamsChange}
          disabled={isProcessing || files.length === 0}
        />

        <AIControlPanel
          currentParams={params}
          onParamsChange={handleParamsChange}
          onAutoOptimize={handleAutoOptimize}
          imageData={rawImageData}
          imageWidth={rawImageWidth}
          imageHeight={rawImageHeight}
          isProcessing={isProcessing}
          onOpenTraining={() => setShowTrainingDialog(true)}
        />
      </div>

      <BatchQueue
        files={files}
        currentIndex={currentIndex}
        onRemove={handleRemoveFile}
        onClear={handleClearQueue}
      />

      {showExportDialog && (
        <ExportDialog
          image={processedImage}
          onExport={handleExport}
          onClose={() => setShowExportDialog(false)}
        />
      )}

      {showTrainingDialog && (
        <TrainingDialog
          onClose={() => setShowTrainingDialog(false)}
          onComplete={handleTrainingComplete}
        />
      )}

      {!wasmReady && (
        <div
          style={{
            position: 'fixed',
            top: 0,
            left: 0,
            right: 0,
            bottom: 0,
            backgroundColor: 'rgba(0,0,0,0.9)',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            zIndex: 2000,
          }}
        >
          <div style={{ textAlign: 'center', color: '#fff' }}>
            <div style={{ fontSize: '48px', marginBottom: '16px' }}>⚙️</div>
            <div style={{ fontSize: '18px' }}>WASM模块加载中...</div>
            <div style={{ fontSize: '12px', color: '#888', marginTop: '8px' }}>
              请确保已使用 wasm-pack 编译 Rust 代码
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default App;
