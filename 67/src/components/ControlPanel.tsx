import React from 'react';
import { ProcessingParams } from '../types';

interface ControlPanelProps {
  params: ProcessingParams;
  onParamsChange: (params: ProcessingParams) => void;
  disabled: boolean;
}

export const ControlPanel: React.FC<ControlPanelProps> = ({
  params,
  onParamsChange,
  disabled,
}) => {
  const handleChange = (key: keyof ProcessingParams, value: any) => {
    onParamsChange({
      ...params,
      [key]: value,
    });
  };

  return (
    <div
      style={{
        width: '300px',
        backgroundColor: '#252525',
        padding: '20px',
        overflowY: 'auto',
        borderLeft: '1px solid #333',
      }}
    >
      <h3 style={{ marginBottom: '20px', color: '#fff', fontSize: '18px' }}>
        处理参数
      </h3>

      <div style={{ marginBottom: '24px' }}>
        <label style={labelStyle}>色调映射算法</label>
        <select
          style={selectStyle}
          value={params.toneMap}
          onChange={(e) => handleChange('toneMap', e.target.value)}
          disabled={disabled}
        >
          <option value="reinhard">Reinhard</option>
          <option value="filmic">Filmic</option>
          <option value="aces">ACES</option>
        </select>
      </div>

      <div style={{ marginBottom: '24px' }}>
        <div style={sliderHeaderStyle}>
          <label style={labelStyle}>曝光</label>
          <span style={valueStyle}>{params.exposure.toFixed(2)} EV</span>
        </div>
        <input
          type="range"
          min="-3"
          max="3"
          step="0.1"
          value={params.exposure}
          onChange={(e) => handleChange('exposure', parseFloat(e.target.value))}
          style={sliderStyle}
          disabled={disabled}
        />
        <div style={rangeLabelsStyle}>
          <span>-3</span>
          <span>0</span>
          <span>+3</span>
        </div>
      </div>

      <div style={{ marginBottom: '24px' }}>
        <div style={sliderHeaderStyle}>
          <label style={labelStyle}>对比度</label>
          <span style={valueStyle}>{params.contrast.toFixed(2)}</span>
        </div>
        <input
          type="range"
          min="0.5"
          max="2"
          step="0.05"
          value={params.contrast}
          onChange={(e) => handleChange('contrast', parseFloat(e.target.value))}
          style={sliderStyle}
          disabled={disabled}
        />
        <div style={rangeLabelsStyle}>
          <span>0.5</span>
          <span>1.0</span>
          <span>2.0</span>
        </div>
      </div>

      <div style={{ marginBottom: '24px' }}>
        <div style={sliderHeaderStyle}>
          <label style={labelStyle}>饱和度</label>
          <span style={valueStyle}>{params.saturation.toFixed(2)}</span>
        </div>
        <input
          type="range"
          min="0"
          max="2"
          step="0.05"
          value={params.saturation}
          onChange={(e) => handleChange('saturation', parseFloat(e.target.value))}
          style={sliderStyle}
          disabled={disabled}
        />
        <div style={rangeLabelsStyle}>
          <span>0</span>
          <span>1.0</span>
          <span>2.0</span>
        </div>
      </div>

      <div style={{ marginBottom: '24px' }}>
        <div style={sliderHeaderStyle}>
          <label style={labelStyle}>高光恢复</label>
          <span style={valueStyle}>{params.highlights.toFixed(2)}</span>
        </div>
        <input
          type="range"
          min="-1"
          max="1"
          step="0.05"
          value={params.highlights}
          onChange={(e) => handleChange('highlights', parseFloat(e.target.value))}
          style={sliderStyle}
          disabled={disabled}
        />
        <div style={rangeLabelsStyle}>
          <span>-1</span>
          <span>0</span>
          <span>+1</span>
        </div>
      </div>

      <div style={{ marginBottom: '24px' }}>
        <div style={sliderHeaderStyle}>
          <label style={labelStyle}>阴影恢复</label>
          <span style={valueStyle}>{params.shadows.toFixed(2)}</span>
        </div>
        <input
          type="range"
          min="-1"
          max="1"
          step="0.05"
          value={params.shadows}
          onChange={(e) => handleChange('shadows', parseFloat(e.target.value))}
          style={sliderStyle}
          disabled={disabled}
        />
        <div style={rangeLabelsStyle}>
          <span>-1</span>
          <span>0</span>
          <span>+1</span>
        </div>
      </div>

      <div style={{ marginBottom: '24px' }}>
        <div style={sliderHeaderStyle}>
          <label style={labelStyle}>色温</label>
          <span style={valueStyle}>{params.temperature.toFixed(2)}</span>
        </div>
        <input
          type="range"
          min="-1"
          max="1"
          step="0.05"
          value={params.temperature}
          onChange={(e) => handleChange('temperature', parseFloat(e.target.value))}
          style={sliderStyle}
          disabled={disabled}
        />
        <div style={rangeLabelsStyle}>
          <span>冷</span>
          <span>中性</span>
          <span>暖</span>
        </div>
      </div>

      <div style={{ marginBottom: '24px' }}>
        <div style={sliderHeaderStyle}>
          <label style={labelStyle}>色调</label>
          <span style={valueStyle}>{params.tint.toFixed(2)}</span>
        </div>
        <input
          type="range"
          min="-1"
          max="1"
          step="0.05"
          value={params.tint}
          onChange={(e) => handleChange('tint', parseFloat(e.target.value))}
          style={sliderStyle}
          disabled={disabled}
        />
        <div style={rangeLabelsStyle}>
          <span>绿</span>
          <span>0</span>
          <span>洋红</span>
        </div>
      </div>

      <button
        onClick={() => onParamsChange(ProcessingParamsDefault())}
        style={{
          width: '100%',
          padding: '12px',
          backgroundColor: '#444',
          color: '#fff',
          border: 'none',
          borderRadius: '4px',
          cursor: 'pointer',
          fontSize: '14px',
          marginTop: '16px',
        }}
        disabled={disabled}
      >
        重置参数
      </button>
    </div>
  );
};

const ProcessingParamsDefault = (): ProcessingParams => ({
  toneMap: 'reinhard',
  exposure: 0,
  contrast: 1,
  saturation: 1,
  highlights: 0,
  shadows: 0,
  temperature: 0,
  tint: 0,
});

const labelStyle: React.CSSProperties = {
  display: 'block',
  marginBottom: '8px',
  fontSize: '14px',
  color: '#ccc',
};

const sliderHeaderStyle: React.CSSProperties = {
  display: 'flex',
  justifyContent: 'space-between',
  alignItems: 'center',
  marginBottom: '8px',
};

const valueStyle: React.CSSProperties = {
  fontSize: '12px',
  color: '#888',
  fontFamily: 'monospace',
};

const sliderStyle: React.CSSProperties = {
  width: '100%',
  height: '4px',
  backgroundColor: '#444',
  borderRadius: '2px',
  outline: 'none',
  cursor: 'pointer',
};

const selectStyle: React.CSSProperties = {
  width: '100%',
  padding: '8px 12px',
  backgroundColor: '#333',
  color: '#fff',
  border: '1px solid #555',
  borderRadius: '4px',
  fontSize: '14px',
};

const rangeLabelsStyle: React.CSSProperties = {
  display: 'flex',
  justifyContent: 'space-between',
  fontSize: '10px',
  color: '#666',
  marginTop: '4px',
};
