import React, { useRef, useEffect, useCallback } from 'react';

const MAX_BARS = 256;
const MAX_VALUE = 1.0;
const MIN_VALUE = 0.0;

function SpectrumVisualizer({ data, fftSize }) {
  const canvasRef = useRef(null);
  const animationRef = useRef(null);
  const dataRef = useRef(null);

  useEffect(() => {
    if (data && data.length > 0) {
      dataRef.current = data;
    }
  }, [data]);

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) {
      animationRef.current = requestAnimationFrame(draw);
      return;
    }

    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();

    if (rect.width <= 0 || rect.height <= 0) {
      animationRef.current = requestAnimationFrame(draw);
      return;
    }

    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.scale(dpr, dpr);

    const width = rect.width;
    const height = rect.height;

    ctx.fillStyle = 'rgba(0, 0, 0, 0.3)';
    ctx.fillRect(0, 0, width, height);

    drawGrid(ctx, width, height);

    const spectrumData = dataRef.current;
    if (!spectrumData || spectrumData.length === 0) {
      animationRef.current = requestAnimationFrame(draw);
      return;
    }

    const numBars = Math.min(spectrumData.length, MAX_BARS);
    if (numBars <= 0) {
      animationRef.current = requestAnimationFrame(draw);
      return;
    }

    const barWidth = width / numBars;
    const gap = Math.min(2, barWidth * 0.1);
    const barSpacing = barWidth - gap;

    if (barSpacing <= 0) {
      animationRef.current = requestAnimationFrame(draw);
      return;
    }

    for (let i = 0; i < numBars; i++) {
      const index = Math.floor((i / numBars) * spectrumData.length);
      const safeIndex = Math.min(index, spectrumData.length - 1);
      const rawValue = spectrumData[safeIndex] || 0;
      
      const value = clampValue(rawValue, MIN_VALUE, MAX_VALUE);
      const barHeight = value * (height - 20);
      const x = i * barWidth + gap / 2;
      const y = Math.max(0, height - barHeight);

      const hue = calculateHue(i, numBars);
      const gradient = ctx.createLinearGradient(0, y, 0, height);
      gradient.addColorStop(0, `hsla(${hue}, 100%, 60%, 0.9)`);
      gradient.addColorStop(1, `hsla(${hue}, 100%, 40%, 0.5)`);

      ctx.fillStyle = gradient;
      
      const radius = Math.min(barSpacing / 2, 4);
      safeRoundRect(ctx, x, y, barSpacing, barHeight, [radius, radius, 0, 0]);
      ctx.fill();

      if (barHeight > 5) {
        ctx.fillStyle = `hsla(${hue}, 100%, 70%, 0.8)`;
        safeRoundRect(ctx, x, y, barSpacing, Math.min(3, barHeight), [1, 1, 0, 0]);
        ctx.fill();
      }
    }

    animationRef.current = requestAnimationFrame(draw);
  }, []);

  useEffect(() => {
    animationRef.current = requestAnimationFrame(draw);
    return () => {
      if (animationRef.current) {
        cancelAnimationFrame(animationRef.current);
      }
    };
  }, [draw]);

  return (
    <div className="spectrum-container">
      <canvas ref={canvasRef} className="spectrum-canvas" />
      <div className="spectrum-overlay">
        FFT Size: {fftSize} | Bars: {Math.min(fftSize / 2, MAX_BARS)}
      </div>
    </div>
  );
}

function drawGrid(ctx, width, height) {
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
  ctx.lineWidth = 1;
  
  for (let i = 1; i < 5; i++) {
    const y = (height / 5) * i;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }
}

function clampValue(value, min, max) {
  if (!isFinite(value)) return min;
  return Math.max(min, Math.min(max, value));
}

function calculateHue(index, total) {
  const normalizedIndex = index / Math.max(total - 1, 1);
  return normalizedIndex * 120 + 180;
}

function safeRoundRect(ctx, x, y, width, height, radii) {
  const r = radii || [0, 0, 0, 0];
  const r0 = r[0] || 0;
  const r1 = r[1] || 0;
  const r2 = r[2] || 0;
  const r3 = r[3] || 0;

  const maxRadius = Math.min(width / 2, height / 2);
  const safeR0 = Math.min(r0, maxRadius);
  const safeR1 = Math.min(r1, maxRadius);
  const safeR2 = Math.min(r2, maxRadius);
  const safeR3 = Math.min(r3, maxRadius);

  ctx.beginPath();
  ctx.moveTo(x + safeR0, y);
  
  ctx.lineTo(x + width - safeR1, y);
  ctx.quadraticCurveTo(x + width, y, x + width, y + safeR1);
  
  ctx.lineTo(x + width, y + height - safeR2);
  ctx.quadraticCurveTo(x + width, y + height, x + width - safeR2, y + height);
  
  ctx.lineTo(x + safeR3, y + height);
  ctx.quadraticCurveTo(x, y + height, x, y + height - safeR3);
  
  ctx.lineTo(x, y + safeR0);
  ctx.quadraticCurveTo(x, y, x + safeR0, y);
  
  ctx.closePath();
}

export default SpectrumVisualizer;
