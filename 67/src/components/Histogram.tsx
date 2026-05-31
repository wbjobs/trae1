import React, { useEffect, useRef } from 'react';
import { HistogramData } from '../types';

interface HistogramProps {
  data: HistogramData | null;
  originalData: HistogramData | null;
  showOriginal: boolean;
}

export const Histogram: React.FC<HistogramProps> = ({
  data,
  originalData,
  showOriginal,
}) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const originalCanvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    if (!canvasRef.current || !data) return;

    const canvas = canvasRef.current;
    const ctx = canvas.getContext('2d');

    if (!ctx) return;

    const width = canvas.width;
    const height = canvas.height;

    ctx.clearRect(0, 0, width, height);

    ctx.fillStyle = '#1a1a1a';
    ctx.fillRect(0, 0, width, height);

    const drawChannel = (
      channelData: number[],
      color: string,
      opacity: number = 0.7
    ) => {
      const maxValue = Math.max(...channelData, 1);

      ctx.beginPath();
      ctx.strokeStyle = color;
      ctx.lineWidth = 1;
      ctx.globalAlpha = opacity;

      channelData.forEach((value, index) => {
        const x = (index / (channelData.length - 1)) * width;
        const y = height - (value / maxValue) * height * 0.9;

        if (index === 0) {
          ctx.moveTo(x, y);
        } else {
          ctx.lineTo(x, y);
        }
      });

      ctx.stroke();

      ctx.lineTo(width, height);
      ctx.lineTo(0, height);
      ctx.closePath();

      ctx.fillStyle = color;
      ctx.globalAlpha = opacity * 0.3;
      ctx.fill();
    };

    drawChannel(data.r, '#ff4444');
    drawChannel(data.g, '#44ff44');
    drawChannel(data.b, '#4444ff');
    drawChannel(data.luminance, '#ffffff', 0.5);

    ctx.globalAlpha = 1;
  }, [data]);

  useEffect(() => {
    if (!originalCanvasRef.current || !originalData) return;

    const canvas = originalCanvasRef.current;
    const ctx = canvas.getContext('2d');

    if (!ctx) return;

    const width = canvas.width;
    const height = canvas.height;

    ctx.clearRect(0, 0, width, height);

    ctx.fillStyle = '#1a1a1a';
    ctx.fillRect(0, 0, width, height);

    const drawChannel = (
      channelData: number[],
      color: string,
      opacity: number = 0.7
    ) => {
      const maxValue = Math.max(...channelData, 1);

      ctx.beginPath();
      ctx.strokeStyle = color;
      ctx.lineWidth = 1;
      ctx.globalAlpha = opacity;

      channelData.forEach((value, index) => {
        const x = (index / (channelData.length - 1)) * width;
        const y = height - (value / maxValue) * height * 0.9;

        if (index === 0) {
          ctx.moveTo(x, y);
        } else {
          ctx.lineTo(x, y);
        }
      });

      ctx.stroke();

      ctx.lineTo(width, height);
      ctx.lineTo(0, height);
      ctx.closePath();

      ctx.fillStyle = color;
      ctx.globalAlpha = opacity * 0.3;
      ctx.fill();
    };

    drawChannel(originalData.r, '#ff4444');
    drawChannel(originalData.g, '#44ff44');
    drawChannel(originalData.b, '#4444ff');
    drawChannel(originalData.luminance, '#ffffff', 0.5);

    ctx.globalAlpha = 1;
  }, [originalData]);

  if (!data && !originalData) {
    return (
      <div
        style={{
          backgroundColor: '#1a1a1a',
          padding: '12px',
          borderRadius: '4px',
          textAlign: 'center',
          color: '#666',
          fontSize: '12px',
        }}
      >
        暂无直方图数据
      </div>
    );
  }

  return (
    <div style={{ backgroundColor: '#1a1a1a', padding: '12px', borderRadius: '4px' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '8px' }}>
        <span style={{ fontSize: '12px', color: '#888' }}>直方图</span>
        <span style={{ fontSize: '10px', color: '#666' }}>
          {showOriginal && originalData ? '对比模式' : '处理后'}
        </span>
      </div>

      <div style={{ position: 'relative' }}>
        <canvas
          ref={canvasRef}
          width={260}
          height={80}
          style={{
            display: 'block',
            width: '100%',
            height: '80px',
          }}
        />

        {showOriginal && originalData && (
          <canvas
            ref={originalCanvasRef}
            width={260}
            height={80}
            style={{
              position: 'absolute',
              top: 0,
              left: 0,
              width: '100%',
              height: '80px',
              opacity: 0.5,
              mixBlendMode: 'screen',
            }}
          />
        )}
      </div>

      <div
        style={{
          display: 'flex',
          justifyContent: 'space-between',
          marginTop: '8px',
          fontSize: '10px',
        }}
      >
        <span style={{ color: '#ff4444' }}>● R</span>
        <span style={{ color: '#44ff44' }}>● G</span>
        <span style={{ color: '#4444ff' }}>● B</span>
        <span style={{ color: '#ffffff' }}>● L</span>
      </div>
    </div>
  );
};
