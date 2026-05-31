import React, { useState, useEffect } from 'react';
import { ProcessingParams, StylePreset, CustomStyleConfig, ImageAnalysis, AutoOptimizeResult } from '../types';
import { getStylePresets, getCustomStyles, autoOptimize, analyzeImageData } from '../services/aiService';

interface AIControlPanelProps {
  currentParams: ProcessingParams;
  onParamsChange: (params: ProcessingParams) => void;
  onAutoOptimize: (result: AutoOptimizeResult) => void;
  imageData: Float32Array | null;
  imageWidth: number;
  imageHeight: number;
  isProcessing: boolean;
  onOpenTraining: () => void;
}

export const AIControlPanel: React.FC<AIControlPanelProps> = ({
  currentParams,
  onParamsChange,
  onAutoOptimize,
  imageData,
  imageWidth,
  imageHeight,
  isProcessing,
  onOpenTraining,
}) => {
  const [selectedPreset, setSelectedPreset] = useState<string>('');
  const [customStyles, setCustomStyles] = useState<CustomStyleConfig[]>([]);
  const [showAutoOptimize, setShowAutoOptimize] = useState(false);
  const [autoOptimizeResult, setAutoOptimizeResult] = useState<AutoOptimizeResult | null>(null);
  const [styleStrength, setStyleStrength] = useState(0.8);
  const [analysis, setAnalysis] = useState<ImageAnalysis | null>(null);

  useEffect(() => {
    setCustomStyles(getCustomStyles());
  }, []);

  useEffect(() => {
    if (imageData && imageWidth > 0 && imageHeight > 0) {
      const result = analyzeImageData(imageData, imageWidth, imageHeight);
      setAnalysis(result);
    }
  }, [imageData, imageWidth, imageHeight]);

  const presetList = getStylePresets();

  const handlePresetSelect = (presetName: string) => {
    setSelectedPreset(presetName);
    const preset = presetList.find(p => p.name === presetName);
    if (preset) {
      onParamsChange(preset.params);
    }
  };

  const handleCustomStyleSelect = (style: CustomStyleConfig) => {
    onParamsChange(style.params);
    setSelectedPreset(style.name);
  };

  const handleAutoOptimize = () => {
    if (!imageData || imageWidth === 0 || imageHeight === 0) return;

    const result = autoOptimize(analysis || analyzeImageData(imageData, imageWidth, imageHeight));
    setAutoOptimizeResult(result);
    setShowAutoOptimize(true);
    onAutoOptimize(result);
  };

  const handleApplyOptimize = () => {
    if (autoOptimizeResult) {
      onParamsChange(autoOptimizeResult.params);
    }
    setShowAutoOptimize(false);
  };

  const handleCloseOptimize = () => {
    setShowAutoOptimize(false);
  };

  const getPresetPreviewColor = (params: ProcessingParams): string => {
    const r = Math.round(Math.max(0, Math.min(255, 128 + (params.temperature || 0) * 50 + params.exposure * 30)));
    const g = Math.round(Math.max(0, Math.min(255, 128 + params.exposure * 30)));
    const b = Math.round(Math.max(0, Math.min(255, 128 - (params.temperature || 0) * 50 + params.exposure * 30)));
    return `rgb(${r}, ${g}, ${b})`;
  };

  return (
    <div style={{
      width: '300px',
      backgroundColor: '#252525',
      padding: '20px',
      overflowY: 'auto',
      borderLeft: '1px solid #333',
    }}>
      <h3 style={{ marginBottom: '16px', color: '#fff', fontSize: '18px' }}>
        🤖 AI辅助调色
      </h3>

      <div style={{ marginBottom: '24px' }}>
        <button
          onClick={handleAutoOptimize}
          disabled={isProcessing || !imageData}
          style={{
            width: '100%',
            padding: '12px',
            backgroundColor: '#4CAF50',
            color: '#fff',
            border: 'none',
            borderRadius: '6px',
            cursor: isProcessing || !imageData ? 'not-allowed' : 'pointer',
            fontSize: '14px',
            fontWeight: 'bold',
            opacity: isProcessing || !imageData ? 0.5 : 1,
          }}
        >
          ✨ 自动优化
        </button>
        <p style={{ fontSize: '11px', color: '#888', marginTop: '8px', textAlign: 'center' }}>
          AI分析图像并推荐最佳参数
        </p>
      </div>

      {analysis && (
        <div style={{ marginBottom: '24px', padding: '12px', backgroundColor: '#1a1a1a', borderRadius: '6px' }}>
          <div style={{ fontSize: '12px', color: '#888', marginBottom: '8px' }}>图像分析</div>
          <div style={{ fontSize: '11px', color: '#aaa', lineHeight: '1.8' }}>
            <div>亮度: {(analysis.avg_luminance * 100).toFixed(0)}%</div>
            <div>动态范围: {(analysis.dynamic_range * 100).toFixed(0)}%</div>
            <div>饱和度: {(analysis.color_saturation * 100).toFixed(0)}%</div>
            <div>对比度: {analysis.contrast_ratio.toFixed(2)}</div>
          </div>
        </div>
      )}

      <div style={{ marginBottom: '16px' }}>
        <label style={{ fontSize: '14px', color: '#ccc', marginBottom: '8px', display: 'block' }}>
          风格预设
        </label>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '8px' }}>
          {presetList.map((preset) => (
            <button
              key={preset.name}
              onClick={() => handlePresetSelect(preset.name)}
              style={{
                padding: '10px 8px',
                backgroundColor: selectedPreset === preset.name ? '#4CAF50' : '#333',
                color: '#fff',
                border: selectedPreset === preset.name ? '2px solid #4CAF50' : '1px solid #444',
                borderRadius: '6px',
                cursor: 'pointer',
                fontSize: '11px',
                textAlign: 'left',
                display: 'flex',
                alignItems: 'center',
                gap: '8px',
              }}
              title={preset.description}
            >
              <div
                style={{
                  width: '20px',
                  height: '20px',
                  borderRadius: '50%',
                  backgroundColor: getPresetPreviewColor(preset.params),
                  border: '1px solid #555',
                  flexShrink: 0,
                }}
              />
              <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                {preset.name}
              </span>
            </button>
          ))}
        </div>
      </div>

      {customStyles.length > 0 && (
        <div style={{ marginBottom: '24px' }}>
          <label style={{ fontSize: '14px', color: '#ccc', marginBottom: '8px', display: 'block' }}>
            我的风格
          </label>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
            {customStyles.map((style) => (
              <button
                key={style.name}
                onClick={() => handleCustomStyleSelect(style)}
                style={{
                  padding: '10px',
                  backgroundColor: selectedPreset === style.name ? '#2a5a2a' : '#1a1a1a',
                  color: '#fff',
                  border: selectedPreset === style.name ? '1px solid #4CAF50' : '1px solid #333',
                  borderRadius: '6px',
                  cursor: 'pointer',
                  fontSize: '12px',
                  textAlign: 'left',
                }}
              >
                <div style={{ fontWeight: 'bold', marginBottom: '4px' }}>{style.name}</div>
                <div style={{ fontSize: '10px', color: '#888' }}>{style.description}</div>
              </button>
            ))}
          </div>
        </div>
      )}

      <div style={{ marginBottom: '24px' }}>
        <label style={{ fontSize: '14px', color: '#ccc', marginBottom: '8px', display: 'block' }}>
          风格强度: {(styleStrength * 100).toFixed(0)}%
        </label>
        <input
          type="range"
          min="0"
          max="1"
          step="0.05"
          value={styleStrength}
          onChange={(e) => setStyleStrength(parseFloat(e.target.value))}
          style={{ width: '100%' }}
        />
      </div>

      <button
        onClick={onOpenTraining}
        style={{
          width: '100%',
          padding: '12px',
          backgroundColor: '#555',
          color: '#fff',
          border: 'none',
          borderRadius: '6px',
          cursor: 'pointer',
          fontSize: '14px',
        }}
      >
        🎨 训练自定义风格
      </button>

      {showAutoOptimize && autoOptimizeResult && (
        <div
          style={{
            position: 'fixed',
            top: 0,
            left: 0,
            width: '100%',
            height: '100%',
            backgroundColor: 'rgba(0,0,0,0.8)',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            zIndex: 1000,
          }}
          onClick={handleCloseOptimize}
        >
          <div
            style={{
              backgroundColor: '#2a2a2a',
              borderRadius: '8px',
              padding: '24px',
              minWidth: '400px',
              maxWidth: '500px',
            }}
            onClick={(e) => e.stopPropagation()}
          >
            <h3 style={{ color: '#fff', marginBottom: '16px' }}>✨ AI自动优化结果</h3>
            
            <div style={{ marginBottom: '16px', padding: '12px', backgroundColor: '#1a1a1a', borderRadius: '6px' }}>
              <div style={{ fontSize: '12px', color: '#888', marginBottom: '8px' }}>推荐参数</div>
              <div style={{ fontSize: '13px', color: '#ddd', lineHeight: '1.8' }}>
                <div>曝光: {autoOptimizeResult.params.exposure.toFixed(2)} EV</div>
                <div>对比度: {autoOptimizeResult.params.contrast.toFixed(2)}</div>
                <div>饱和度: {autoOptimizeResult.params.saturation.toFixed(2)}</div>
                <div>高光: {autoOptimizeResult.params.highlights.toFixed(2)}</div>
                <div>阴影: {autoOptimizeResult.params.shadows.toFixed(2)}</div>
                <div>色调映射: {autoOptimizeResult.params.toneMap}</div>
              </div>
            </div>

            <div style={{ marginBottom: '20px' }}>
              <div style={{ fontSize: '12px', color: '#888', marginBottom: '8px' }}>分析结果</div>
              <div style={{ fontSize: '12px', color: '#aaa' }}>
                {autoOptimizeResult.recommendation}
              </div>
              <div style={{ fontSize: '11px', color: '#888', marginTop: '8px' }}>
                置信度: {(autoOptimizeResult.confidence * 100).toFixed(0)}%
              </div>
            </div>

            <div style={{ display: 'flex', gap: '12px' }}>
              <button
                onClick={handleCloseOptimize}
                style={{
                  flex: 1,
                  padding: '12px',
                  backgroundColor: '#444',
                  color: '#fff',
                  border: 'none',
                  borderRadius: '6px',
                  cursor: 'pointer',
                  fontSize: '14px',
                }}
              >
                取消
              </button>
              <button
                onClick={handleApplyOptimize}
                style={{
                  flex: 1,
                  padding: '12px',
                  backgroundColor: '#4CAF50',
                  color: '#fff',
                  border: 'none',
                  borderRadius: '6px',
                  cursor: 'pointer',
                  fontSize: '14px',
                }}
              >
                应用参数
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
