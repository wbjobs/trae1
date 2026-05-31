import { useEffect, useRef } from 'react';
import { Entity, Obstacle, defaultObstacles } from '../physics/world';

interface Props {
  predicted: Entity | null;
  authority: { x: number; y: number } | null;
  otherPlayers: Entity[];
  obstacles?: Obstacle[];
  correctionEvents: { frame: number; diff: number }[];
  width?: number;
  height?: number;
}

export default function RollbackDebugger({
  predicted,
  authority,
  otherPlayers,
  obstacles = defaultObstacles,
  correctionEvents,
  width = 800,
  height = 600,
}: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const scale = 1;
    const ox = width / 2;
    const oy = height / 2;

    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = '#0d1117';
    ctx.fillRect(0, 0, width, height);

    ctx.strokeStyle = '#1a1f2b';
    ctx.lineWidth = 1;
    for (let x = -400; x <= 400; x += 50) {
      ctx.beginPath();
      ctx.moveTo(ox + x * scale, oy - 300 * scale);
      ctx.lineTo(ox + x * scale, oy + 300 * scale);
      ctx.stroke();
    }
    for (let y = -300; y <= 300; y += 50) {
      ctx.beginPath();
      ctx.moveTo(ox - 400 * scale, oy + y * scale);
      ctx.lineTo(ox + 400 * scale, oy + y * scale);
      ctx.stroke();
    }

    ctx.fillStyle = '#2a3040';
    for (const obs of obstacles) {
      const x = ox + obs.min.x * scale;
      const y = oy + obs.min.y * scale;
      const w = (obs.max.x - obs.min.x) * scale;
      const h = (obs.max.y - obs.min.y) * scale;
      ctx.fillRect(x, y, w, h);
      ctx.strokeStyle = '#3a4050';
      ctx.lineWidth = 2;
      ctx.strokeRect(x, y, w, h);
    }

    for (const op of otherPlayers) {
      const px = ox + op.pos.x * scale;
      const py = oy + op.pos.y * scale;
      ctx.beginPath();
      ctx.arc(px, py, op.radius * scale, 0, Math.PI * 2);
      ctx.fillStyle = '#4a5568';
      ctx.fill();
    }

    if (authority) {
      const ax = ox + authority.x * scale;
      const ay = oy + authority.y * scale;
      ctx.beginPath();
      ctx.arc(ax, ay, 24 * scale, 0, Math.PI * 2);
      ctx.strokeStyle = '#00ff88';
      ctx.lineWidth = 3;
      ctx.stroke();
      ctx.fillStyle = 'rgba(0, 255, 136, 0.1)';
      ctx.fill();
      ctx.fillStyle = '#00ff88';
      ctx.font = '11px monospace';
      ctx.fillText('权威', ax + 18, ay - 18);
    }

    if (predicted) {
      const px = ox + predicted.pos.x * scale;
      const py = oy + predicted.pos.y * scale;
      ctx.beginPath();
      ctx.arc(px, py, predicted.radius * scale, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(58, 122, 254, 0.8)';
      ctx.fill();
      ctx.strokeStyle = '#3a7afe';
      ctx.lineWidth = 2;
      ctx.stroke();
      ctx.fillStyle = '#3a7afe';
      ctx.font = '11px monospace';
      ctx.fillText('预测', px + 18, py - 4);

      const vx = px + predicted.vel.x * 0.1;
      const vy = py + predicted.vel.y * 0.1;
      ctx.beginPath();
      ctx.moveTo(px, py);
      ctx.lineTo(vx, vy);
      ctx.strokeStyle = '#ffaa00';
      ctx.lineWidth = 2;
      ctx.stroke();
    }

    if (predicted && authority) {
      const px = ox + predicted.pos.x * scale;
      const py = oy + predicted.pos.y * scale;
      const ax = ox + authority.x * scale;
      const ay = oy + authority.y * scale;
      const dx = predicted.pos.x - authority.x;
      const dy = predicted.pos.y - authority.y;
      const dist = Math.sqrt(dx * dx + dy * dy);

      ctx.beginPath();
      ctx.moveTo(px, py);
      ctx.lineTo(ax, ay);
      ctx.strokeStyle = dist > 0.5 ? '#ff4444' : '#88ff88';
      ctx.lineWidth = 1.5;
      ctx.setLineDash([4, 4]);
      ctx.stroke();
      ctx.setLineDash([]);

      ctx.fillStyle = '#ffffff';
      ctx.font = '12px monospace';
      ctx.fillText(`差异: ${dist.toFixed(2)} px`, 12, 20);
      const lastCorr = correctionEvents[correctionEvents.length - 1];
      if (lastCorr) {
        ctx.fillText(`校正次数: ${correctionEvents.length}`, 12, 38);
        ctx.fillText(`上次误差: ${lastCorr.diff.toFixed(2)}`, 12, 56);
      }
    }

    ctx.strokeStyle = '#2a3040';
    ctx.lineWidth = 2;
    ctx.strokeRect(0, 0, width, height);
  }, [predicted, authority, otherPlayers, obstacles, correctionEvents, width, height]);

  return (
    <div className="panel">
      <div className="row" style={{ justifyContent: 'space-between', marginBottom: 10 }}>
        <h2 style={{ margin: 0, fontSize: 16 }}>
          🎯 Rollback 调试面板
        </h2>
        <div className="row" style={{ gap: 16, fontSize: 12, fontFamily: 'monospace', color: '#999' }}>
          <span style={{ color: '#3a7afe' }}>● 预测</span>
          <span style={{ color: '#00ff88' }}>○ 权威</span>
          <span style={{ color: '#ff4444' }}>-- 误差</span>
        </div>
      </div>
      <canvas
        ref={canvasRef}
        width={width}
        height={height}
        style={{
          width: '100%',
          maxWidth: width,
          height: 'auto',
          borderRadius: 8,
          background: '#0d1117',
          border: '1px solid #2a3040',
        }}
      />
    </div>
  );
}
