import React, { useEffect, useRef, useState } from 'react';
import { ProcessedImage } from '../types';

interface ImageViewerProps {
  image: ProcessedImage | null;
  originalPreview: ProcessedImage | null;
  showComparison: boolean;
  comparisonPosition: number;
  onComparisonChange: (position: number) => void;
}

export const ImageViewer: React.FC<ImageViewerProps> = ({
  image,
  originalPreview,
  showComparison,
  comparisonPosition,
  onComparisonChange,
}) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const [zoom, setZoom] = useState(1);
  const [isDragging, setIsDragging] = useState(false);
  const [dragStart, setDragStart] = useState({ x: 0, y: 0 });
  const [offset, setOffset] = useState({ x: 0, y: 0 });

  useEffect(() => {
    if (!canvasRef.current || !image) return;

    const canvas = canvasRef.current;
    const ctx = canvas.getContext('2d');

    if (!ctx) return;

    canvas.width = image.width;
    canvas.height = image.height;

    const imageData = new ImageData(
      new Uint8ClampedArray(image.data),
      image.width,
      image.height
    );

    ctx.putImageData(imageData, 0, 0);
  }, [image]);

  useEffect(() => {
    if (!containerRef.current) return;

    const container = containerRef.current;
    const handleWheel = (e: WheelEvent) => {
      e.preventDefault();
      const delta = e.deltaY > 0 ? 0.9 : 1.1;
      setZoom((prev) => Math.max(0.1, Math.min(5, prev * delta)));
    };

    container.addEventListener('wheel', handleWheel, { passive: false });
    return () => container.removeEventListener('wheel', handleWheel);
  }, []);

  const handleMouseDown = (e: React.MouseEvent) => {
    setIsDragging(true);
    setDragStart({ x: e.clientX - offset.x, y: e.clientY - offset.y });
  };

  const handleMouseMove = (e: React.MouseEvent) => {
    if (!isDragging) return;
    setOffset({
      x: e.clientX - dragStart.x,
      y: e.clientY - dragStart.y,
    });
  };

  const handleMouseUp = () => {
    setIsDragging(false);
  };

  const handleSliderChange = (e: React.MouseEvent) => {
    if (!containerRef.current) return;
    const rect = containerRef.current.getBoundingClientRect();
    const position = ((e.clientX - rect.left) / rect.width) * 100;
    onComparisonChange(Math.max(0, Math.min(100, position)));
  };

  return (
    <div
      ref={containerRef}
      style={{
        flex: 1,
        backgroundColor: '#0a0a0a',
        overflow: 'hidden',
        position: 'relative',
        cursor: isDragging ? 'grabbing' : 'grab',
      }}
      onMouseDown={handleMouseDown}
      onMouseMove={handleMouseMove}
      onMouseUp={handleMouseUp}
      onMouseLeave={handleMouseUp}
    >
      {image ? (
        <div
          style={{
            width: '100%',
            height: '100%',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            position: 'relative',
          }}
        >
          {showComparison && originalPreview ? (
            <div style={{ position: 'relative', width: '100%', height: '100%' }}>
              <canvas
                ref={canvasRef}
                style={{
                  position: 'absolute',
                  top: '50%',
                  left: '50%',
                  transform: `translate(-50%, -50%) translate(${offset.x}px, ${offset.y}px) scale(${zoom})`,
                  maxWidth: '100%',
                  maxHeight: '100%',
                  clipPath: `inset(0 ${100 - comparisonPosition}% 0 0)`,
                }}
              />
              <canvas
                style={{
                  position: 'absolute',
                  top: '50%',
                  left: '50%',
                  transform: `translate(-50%, -50%) translate(${offset.x}px, ${offset.y}px) scale(${zoom})`,
                  maxWidth: '100%',
                  maxHeight: '100%',
                  clipPath: `inset(0 0 0 ${comparisonPosition}%)`,
                  opacity: 0,
                }}
                width={originalPreview.width}
                height={originalPreview.height}
                ref={(el) => {
                  if (el) {
                    const ctx = el.getContext('2d');
                    if (ctx) {
                      const imageData = new ImageData(
                        new Uint8ClampedArray(originalPreview.data),
                        originalPreview.width,
                        originalPreview.height
                      );
                      ctx.putImageData(imageData, 0, 0);
                    }
                  }
                }}
              />
              <div
                style={{
                  position: 'absolute',
                  top: 0,
                  left: `${comparisonPosition}%`,
                  width: '2px',
                  height: '100%',
                  backgroundColor: '#fff',
                  cursor: 'ew-resize',
                  transform: 'translateX(-50%)',
                }}
                onMouseDown={handleSliderChange}
              />
              <div
                style={{
                  position: 'absolute',
                  top: '10px',
                  left: '10px',
                  backgroundColor: 'rgba(0,0,0,0.7)',
                  padding: '4px 8px',
                  borderRadius: '4px',
                  fontSize: '12px',
                  color: '#fff',
                }}
              >
                原图
              </div>
              <div
                style={{
                  position: 'absolute',
                  top: '10px',
                  right: '10px',
                  backgroundColor: 'rgba(0,0,0,0.7)',
                  padding: '4px 8px',
                  borderRadius: '4px',
                  fontSize: '12px',
                  color: '#fff',
                }}
              >
                处理后
              </div>
            </div>
          ) : (
            <canvas
              ref={canvasRef}
              style={{
                transform: `translate(${offset.x}px, ${offset.y}px) scale(${zoom})`,
                maxWidth: '100%',
                maxHeight: '100%',
              }}
            />
          )}
        </div>
      ) : (
        <div
          style={{
            width: '100%',
            height: '100%',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            color: '#666',
            fontSize: '18px',
          }}
        >
          请上传RAW格式图片进行处理
        </div>
      )}

      {image && (
        <div
          style={{
            position: 'absolute',
            bottom: '16px',
            left: '50%',
            transform: 'translateX(-50%)',
            backgroundColor: 'rgba(0,0,0,0.8)',
            padding: '8px 16px',
            borderRadius: '20px',
            display: 'flex',
            alignItems: 'center',
            gap: '16px',
          }}
        >
          <button
            onClick={() => setZoom((z) => Math.max(0.1, z * 0.9))}
            style={zoomButtonStyle}
          >
            -
          </button>
          <span style={{ fontSize: '12px', minWidth: '50px', textAlign: 'center' }}>
            {(zoom * 100).toFixed(0)}%
          </span>
          <button
            onClick={() => setZoom((z) => Math.min(5, z * 1.1))}
            style={zoomButtonStyle}
          >
            +
          </button>
          <button
            onClick={() => {
              setZoom(1);
              setOffset({ x: 0, y: 0 });
            }}
            style={zoomButtonStyle}
          >
            重置
          </button>
        </div>
      )}
    </div>
  );
};

const zoomButtonStyle: React.CSSProperties = {
  width: '28px',
  height: '28px',
  borderRadius: '50%',
  border: 'none',
  backgroundColor: '#444',
  color: '#fff',
  cursor: 'pointer',
  fontSize: '16px',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
};
