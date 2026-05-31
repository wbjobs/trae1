import { useEffect, useState } from 'react';
import { TransportClient, TransportStats } from '../transport';

interface Props {
  transport: TransportClient | null;
}

export default function LatencyPanel({ transport }: Props) {
  const [stats, setStats] = useState<TransportStats>({ rtt: 0, loss: 0, sent: 0, acked: 0, p95: 0, reorderEvents: 0 });
  const [history, setHistory] = useState<number[]>([]);

  useEffect(() => {
    if (!transport) return;
    const off = transport.onStats((s) => {
      setStats(s);
      setHistory((h) => [...h.slice(-59), s.rtt]);
    });
    return () => { off(); };
  }, [transport]);

  return (
    <div className="panel">
      <div className="row" style={{ justifyContent: 'space-between' }}>
        <h2 style={{ margin: 0, fontSize: 16 }}>延迟 / 丢包测试</h2>
        <div className="row">
          <button
            className="secondary"
            onClick={() => transport?.sendPing(1024)}
          >手动 Ping</button>
        </div>
      </div>
      <div className="metrics" style={{ marginTop: 14, gridTemplateColumns: 'repeat(5, 1fr)' }}>
        <div className="metric">
          <div className="label">RTT (当前)</div>
          <div className="value">{stats.rtt.toFixed(1)}ms</div>
        </div>
        <div className="metric">
          <div className="label">P95 延迟</div>
          <div className="value">{stats.p95.toFixed(1)}ms</div>
        </div>
        <div className="metric">
          <div className="label">丢包率</div>
          <div className="value">{stats.loss.toFixed(2)}%</div>
        </div>
        <div className="metric">
          <div className="label">已发送/已确认</div>
          <div className="value">{stats.sent & 0xffff}/{stats.acked & 0xffff}</div>
        </div>
        <div className="metric">
          <div className="label">乱序事件</div>
          <div className="value">{stats.reorderEvents}</div>
        </div>
      </div>

      <svg viewBox="0 0 600 120" style={{ width: '100%', height: 120, marginTop: 14, background: '#12151b', borderRadius: 6 }}>
        {history.map((v, i) => {
          const h = Math.min(100, v);
          return <rect key={i} x={i * 10} y={120 - h} width="8" height={h} fill="#3a7afe" />;
        })}
      </svg>
    </div>
  );
}
