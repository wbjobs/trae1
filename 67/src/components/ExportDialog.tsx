import React, { useState } from 'react';
import { ProcessedImage } from '../types';

interface ExportDialogProps {
  image: ProcessedImage | null;
  onExport: (format: string, quality: number) => Promise<void>;
  onClose: () => void;
}

export const ExportDialog: React.FC<ExportDialogProps> = ({
  image,
  onExport,
  onClose,
}) => {
  const [format, setFormat] = useState('jpeg');
  const [quality, setQuality] = useState(0.9);
  const [isExporting, setIsExporting] = useState(false);

  const handleExport = async () => {
    if (!image) return;

    setIsExporting(true);
    try {
      await onExport(format, quality);
    } finally {
      setIsExporting(false);
    }
  };

  if (!image) return null;

  return (
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
      onClick={onClose}
    >
      <div
        style={{
          backgroundColor: '#2a2a2a',
          borderRadius: '8px',
          padding: '24px',
          minWidth: '400px',
          boxShadow: '0 8px 32px rgba(0,0,0,0.5)',
        }}
        onClick={(e) => e.stopPropagation()}
      >
        <h3 style={{ marginBottom: '20px', color: '#fff', fontSize: '18px' }}>
          导出图像
        </h3>

        <div style={{ marginBottom: '20px' }}>
          <label style={labelStyle}>文件格式</label>
          <div style={{ display: 'flex', gap: '12px' }}>
            {['jpeg', 'png', 'avif'].map((f) => (
              <button
                key={f}
                onClick={() => setFormat(f)}
                style={{
                  flex: 1,
                  padding: '12px',
                  backgroundColor: format === f ? '#4CAF50' : '#333',
                  color: format === f ? '#fff' : '#aaa',
                  border: 'none',
                  borderRadius: '4px',
                  cursor: 'pointer',
                  fontSize: '14px',
                  textTransform: 'uppercase',
                }}
              >
                {f}
              </button>
            ))}
          </div>
        </div>

        {format === 'jpeg' && (
          <div style={{ marginBottom: '20px' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '8px' }}>
              <label style={labelStyle}>图像质量</label>
              <span style={{ color: '#888', fontSize: '12px' }}>
                {(quality * 100).toFixed(0)}%
              </span>
            </div>
            <input
              type="range"
              min="0.1"
              max="1"
              step="0.05"
              value={quality}
              onChange={(e) => setQuality(parseFloat(e.target.value))}
              style={{ width: '100%' }}
            />
          </div>
        )}

        <div style={{ marginBottom: '20px', padding: '12px', backgroundColor: '#1a1a1a', borderRadius: '4px' }}>
          <div style={{ fontSize: '12px', color: '#888', marginBottom: '4px' }}>图像信息</div>
          <div style={{ fontSize: '14px', color: '#ccc' }}>
            {image.width} × {image.height} 像素
          </div>
        </div>

        <div style={{ display: 'flex', gap: '12px' }}>
          <button
            onClick={onClose}
            style={{
              flex: 1,
              padding: '12px',
              backgroundColor: '#444',
              color: '#fff',
              border: 'none',
              borderRadius: '4px',
              cursor: 'pointer',
              fontSize: '14px',
            }}
          >
            取消
          </button>
          <button
            onClick={handleExport}
            disabled={isExporting}
            style={{
              flex: 1,
              padding: '12px',
              backgroundColor: isExporting ? '#666' : '#4CAF50',
              color: '#fff',
              border: 'none',
              borderRadius: '4px',
              cursor: isExporting ? 'not-allowed' : 'pointer',
              fontSize: '14px',
            }}
          >
            {isExporting ? '导出中...' : '导出'}
          </button>
        </div>
      </div>
    </div>
  );
};

const labelStyle: React.CSSProperties = {
  display: 'block',
  marginBottom: '8px',
  fontSize: '14px',
  color: '#ccc',
};
