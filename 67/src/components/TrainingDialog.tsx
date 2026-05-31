import React, { useState, useRef } from 'react';
import { TrainingConfig, TrainingProgress, CustomStyleConfig } from '../types';
import { trainCustomStyle, getStylePresets } from '../services/aiService';

interface TrainingDialogProps {
  onClose: () => void;
  onComplete: (style: CustomStyleConfig) => void;
}

export const TrainingDialog: React.FC<TrainingDialogProps> = ({ onClose, onComplete }) => {
  const [referenceImages, setReferenceImages] = useState<File[]>([]);
  const [styleName, setStyleName] = useState('');
  const [description, setDescription] = useState('');
  const [baseStyle, setBaseStyle] = useState('filmic');
  const [epochs, setEpochs] = useState(50);
  const [learningRate, setLearningRate] = useState(0.001);
  const [progress, setProgress] = useState<TrainingProgress | null>(null);
  const [isTraining, setIsTraining] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const presetList = getStylePresets();

  const handleFileSelect = (e: React.ChangeEvent<HTMLInputElement>) => {
    const files = Array.from(e.target.files || []);
    const imageFiles = files.filter(f => f.type.startsWith('image/'));
    setReferenceImages(prev => {
      const newFiles = [...prev, ...imageFiles].slice(0, 10);
      return newFiles;
    });
    setError(null);
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    const files = Array.from(e.dataTransfer.files);
    const imageFiles = files.filter(f => f.type.startsWith('image/'));
    setReferenceImages(prev => {
      const newFiles = [...prev, ...imageFiles].slice(0, 10);
      return newFiles;
    });
    setError(null);
  };

  const removeImage = (index: number) => {
    setReferenceImages(prev => prev.filter((_, i) => i !== index));
  };

  const handleTrain = async () => {
    if (referenceImages.length < 5) {
      setError('需要至少5张参考图片');
      return;
    }
    if (!styleName.trim()) {
      setError('请输入风格名称');
      return;
    }

    setError(null);
    setIsTraining(true);

    try {
      const imageDataList: Uint8Array[] = [];
      for (const file of referenceImages) {
        const buffer = await file.arrayBuffer();
        imageDataList.push(new Uint8Array(buffer));
      }

      const config: TrainingConfig = {
        styleName: styleName.trim(),
        description: description.trim(),
        baseStyle,
        epochs,
        learningRate,
      };

      const result = await trainCustomStyle(
        imageDataList,
        config,
        (p) => setProgress(p)
      );

      onComplete(result);
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setIsTraining(false);
    }
  };

  const getPreviewUrl = (file: File): string => {
    return URL.createObjectURL(file);
  };

  return (
    <div
      style={{
        position: 'fixed',
        top: 0,
        left: 0,
        width: '100%',
        height: '100%',
        backgroundColor: 'rgba(0,0,0,0.9)',
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
          borderRadius: '12px',
          padding: '24px',
          width: '600px',
          maxHeight: '90vh',
          overflowY: 'auto',
        }}
        onClick={(e) => e.stopPropagation()}
      >
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '20px' }}>
          <h2 style={{ color: '#fff', fontSize: '20px', margin: 0 }}>🎨 训练自定义风格</h2>
          <button
            onClick={onClose}
            style={{
              background: 'none',
              border: 'none',
              color: '#888',
              fontSize: '24px',
              cursor: 'pointer',
              padding: '0 8px',
            }}
          >
            ×
          </button>
        </div>

        {error && (
          <div style={{
            backgroundColor: '#f44336',
            color: '#fff',
            padding: '12px',
            borderRadius: '6px',
            marginBottom: '16px',
            fontSize: '14px',
          }}>
            {error}
          </div>
        )}

        <div style={{ marginBottom: '20px' }}>
          <label style={{ fontSize: '14px', color: '#ccc', marginBottom: '8px', display: 'block' }}>
            参考图片 ({referenceImages.length}/10)
          </label>
          <div
            style={{
              border: '2px dashed #444',
              borderRadius: '8px',
              padding: '20px',
              minHeight: '150px',
              display: 'flex',
              flexWrap: 'wrap',
              gap: '8px',
              alignItems: 'center',
              justifyContent: referenceImages.length === 0 ? 'center' : 'flex-start',
            }}
            onDragOver={(e) => e.preventDefault()}
            onDrop={handleDrop}
            onClick={() => fileInputRef.current?.click()}
          >
            {referenceImages.length === 0 && (
              <div style={{ textAlign: 'center', color: '#666' }}>
                <div style={{ fontSize: '36px', marginBottom: '8px' }}>📷</div>
                <div style={{ fontSize: '13px' }}>点击或拖放图片到此处</div>
                <div style={{ fontSize: '11px', marginTop: '4px' }}>需要5-10张参考图片</div>
              </div>
            )}

            {referenceImages.map((file, index) => (
              <div
                key={index}
                style={{
                  position: 'relative',
                  width: '80px',
                  height: '80px',
                  borderRadius: '6px',
                  overflow: 'hidden',
                  border: '2px solid #444',
                }}
              >
                <img
                  src={getPreviewUrl(file)}
                  alt={file.name}
                  style={{ width: '100%', height: '100%', objectFit: 'cover' }}
                />
                <button
                  onClick={(e) => {
                    e.stopPropagation();
                    removeImage(index);
                  }}
                  style={{
                    position: 'absolute',
                    top: '2px',
                    right: '2px',
                    width: '20px',
                    height: '20px',
                    borderRadius: '50%',
                    backgroundColor: 'rgba(0,0,0,0.7)',
                    color: '#fff',
                    border: 'none',
                    cursor: 'pointer',
                    fontSize: '12px',
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'center',
                  }}
                >
                  ×
                </button>
              </div>
            ))}
          </div>

          <input
            ref={fileInputRef}
            type="file"
            accept="image/*"
            multiple
            style={{ display: 'none' }}
            onChange={handleFileSelect}
          />
        </div>

        <div style={{ marginBottom: '16px' }}>
          <label style={{ fontSize: '14px', color: '#ccc', marginBottom: '8px', display: 'block' }}>
            风格名称 *
          </label>
          <input
            type="text"
            value={styleName}
            onChange={(e) => setStyleName(e.target.value)}
            placeholder="输入风格名称"
            style={{
              width: '100%',
              padding: '10px 12px',
              backgroundColor: '#1a1a1a',
              color: '#fff',
              border: '1px solid #444',
              borderRadius: '6px',
              fontSize: '14px',
            }}
          />
        </div>

        <div style={{ marginBottom: '16px' }}>
          <label style={{ fontSize: '14px', color: '#ccc', marginBottom: '8px', display: 'block' }}>
            描述
          </label>
          <input
            type="text"
            value={description}
            onChange={(e) => setDescription(e.target.value)}
            placeholder="描述这个风格的特点"
            style={{
              width: '100%',
              padding: '10px 12px',
              backgroundColor: '#1a1a1a',
              color: '#fff',
              border: '1px solid #444',
              borderRadius: '6px',
              fontSize: '14px',
            }}
          />
        </div>

        <div style={{ marginBottom: '16px' }}>
          <label style={{ fontSize: '14px', color: '#ccc', marginBottom: '8px', display: 'block' }}>
            基础风格
          </label>
          <select
            value={baseStyle}
            onChange={(e) => setBaseStyle(e.target.value)}
            style={{
              width: '100%',
              padding: '10px 12px',
              backgroundColor: '#1a1a1a',
              color: '#fff',
              border: '1px solid #444',
              borderRadius: '6px',
              fontSize: '14px',
            }}
          >
            {presetList.map((preset) => (
              <option key={preset.name} value={preset.name}>
                {preset.name} - {preset.description}
              </option>
            ))}
          </select>
        </div>

        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '16px', marginBottom: '20px' }}>
          <div>
            <label style={{ fontSize: '14px', color: '#ccc', marginBottom: '8px', display: 'block' }}>
              训练轮次: {epochs}
            </label>
            <input
              type="range"
              min="10"
              max="100"
              step="10"
              value={epochs}
              onChange={(e) => setEpochs(parseInt(e.target.value))}
              style={{ width: '100%' }}
              disabled={isTraining}
            />
          </div>
          <div>
            <label style={{ fontSize: '14px', color: '#ccc', marginBottom: '8px', display: 'block' }}>
              学习率: {learningRate}
            </label>
            <input
              type="range"
              min="0.0001"
              max="0.01"
              step="0.0001"
              value={learningRate}
              onChange={(e) => setLearningRate(parseFloat(e.target.value))}
              style={{ width: '100%' }}
              disabled={isTraining}
            />
          </div>
        </div>

        {progress && (
          <div style={{
            marginBottom: '20px',
            padding: '16px',
            backgroundColor: '#1a1a1a',
            borderRadius: '8px',
          }}>
            <div style={{ fontSize: '14px', color: '#4CAF50', marginBottom: '8px' }}>
              训练中... ({progress.epoch}/{progress.totalEpochs})
            </div>
            <div style={{
              width: '100%',
              height: '8px',
              backgroundColor: '#333',
              borderRadius: '4px',
              overflow: 'hidden',
              marginBottom: '8px',
            }}>
              <div style={{
                width: `${(progress.epoch / progress.totalEpochs) * 100}%`,
                height: '100%',
                backgroundColor: '#4CAF50',
                transition: 'width 0.3s ease',
              }} />
            </div>
            <div style={{ fontSize: '12px', color: '#888' }}>
              损失: {progress.loss.toFixed(4)} | 准确率: {(progress.accuracy * 100).toFixed(1)}%
            </div>
          </div>
        )}

        <div style={{ display: 'flex', gap: '12px' }}>
          <button
            onClick={onClose}
            disabled={isTraining}
            style={{
              flex: 1,
              padding: '12px',
              backgroundColor: '#444',
              color: '#fff',
              border: 'none',
              borderRadius: '6px',
              cursor: isTraining ? 'not-allowed' : 'pointer',
              fontSize: '14px',
              opacity: isTraining ? 0.5 : 1,
            }}
          >
            取消
          </button>
          <button
            onClick={handleTrain}
            disabled={isTraining || referenceImages.length < 5}
            style={{
              flex: 1,
              padding: '12px',
              backgroundColor: (isTraining || referenceImages.length < 5) ? '#666' : '#4CAF50',
              color: '#fff',
              border: 'none',
              borderRadius: '6px',
              cursor: (isTraining || referenceImages.length < 5) ? 'not-allowed' : 'pointer',
              fontSize: '14px',
              fontWeight: 'bold',
            }}
          >
            {isTraining ? '训练中...' : '开始训练'}
          </button>
        </div>
      </div>
    </div>
  );
};
