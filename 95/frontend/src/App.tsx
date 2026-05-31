import { useEffect, useMemo, useRef, useState } from 'react';
import { TransportClient } from './transport';
import { useGamepads } from './useGamepads';
import { useRollback } from './hooks/useRollback';
import GamepadCard from './components/GamepadCard';
import LatencyPanel from './components/LatencyPanel';
import RollbackDebugger from './components/RollbackDebugger';

const DEFAULT_URL = 'https://localhost:4443/wt';

export default function App() {
  const [url, setUrl] = useState(DEFAULT_URL);
  const [connected, setConnected] = useState(false);
  const [layouts, setLayouts] = useState<Record<number, string>>({});
  const [log, setLog] = useState<string[]>([]);
  const [rollbackEnabled, setRollbackEnabled] = useState(true);
  const [predictionWindow, setPredictionWindow] = useState(100);
  const transportRef = useRef<TransportClient | null>(null);
  const [, force] = useState(0);

  const pads = useGamepads(120);

  const rollback = useRollback(
    connected && rollbackEnabled ? transportRef.current : null,
    pads,
    { predictionWindowMs: predictionWindow, enabled: rollbackEnabled && connected }
  );

  const appendLog = (line: string) => {
    const ts = new Date().toISOString().substring(11, 19);
    setLog((l) => [`[${ts}] ${line}`, ...l].slice(0, 200));
  };

  const connect = async () => {
    try {
      appendLog(`连接 ${url} ...`);
      const t = new TransportClient(url);
      t.playerId = Math.floor(Math.random() * 1000);
      await t.connect();
      transportRef.current = t;
      setConnected(true);
      appendLog(`WebTransport 连接成功 (Datagram 已就绪, playerId=${t.playerId})`);
      force((x) => x + 1);
    } catch (e: any) {
      appendLog(`连接失败: ${e.message ?? e}`);
    }
  };

  const disconnect = () => {
    transportRef.current?.close();
    transportRef.current = null;
    setConnected(false);
    appendLog('已断开');
  };

  // send input loop (only when rollback is disabled)
  useEffect(() => {
    if (!connected || rollbackEnabled) return;
    let raf = 0;
    let last = 0;
    const loop = (t: number) => {
      if (t - last >= 8) { // ~125 Hz
        last = t;
        const t = transportRef.current;
        if (t) {
          for (const p of pads) {
            t.sendInput(p.index, p.buttons, p.axes);
          }
        }
      }
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, [connected, pads, rollbackEnabled]);

  const setLayout = (idx: number, l: string) => {
    setLayouts((prev) => ({ ...prev, [idx]: l }));
    transportRef.current?.sendMapping(l);
    appendLog(`Pad ${idx} 布局切换为 ${l}`);
  };

  const layoutFor = (idx: number) => layouts[idx] ?? detectLayout(pads.find(p => p.index === idx)?.id ?? '');

  const statusClass = useMemo(() => (connected ? 'ok' : 'err'), [connected]);

  return (
    <div className="app">
      <header>
        <h1>Cloud Gamepad · WebTransport</h1>
        <div className={'status ' + statusClass}>
          <span className="dot" />
          {connected ? '已连接' : '未连接'}
        </div>
      </header>

      <div className="panel">
        <div className="row">
          <label>WebTransport URL</label>
          <input
            style={{ width: 360 }}
            value={url}
            onChange={(e) => setUrl(e.target.value)}
          />
          {!connected ? (
            <button onClick={connect}>连接</button>
          ) : (
            <button className="secondary" onClick={disconnect}>断开</button>
          )}
        </div>
      </div>

      <LatencyPanel transport={transportRef.current} />

      <div className="panel">
        <div className="row" style={{ justifyContent: 'space-between', marginBottom: 12 }}>
          <h2 style={{ margin: 0, fontSize: 16 }}>⚙️ Rollback Netcode 配置</h2>
          <label className="row" style={{ gap: 8, cursor: 'pointer' }}>
            <input
              type="checkbox"
              checked={rollbackEnabled}
              onChange={(e) => setRollbackEnabled(e.target.checked)}
            />
            <span style={{ fontSize: 13 }}>启用客户端预测</span>
          </label>
        </div>
        <div className="grid" style={{ gridTemplateColumns: 'repeat(3, 1fr)', gap: 16 }}>
          <div>
            <div className="label" style={{ marginBottom: 6, fontSize: 12, color: '#999' }}>
              预测窗口: {predictionWindow}ms
            </div>
            <input
              type="range"
              min="20"
              max="300"
              step="10"
              value={predictionWindow}
              onChange={(e) => setPredictionWindow(Number(e.target.value))}
              style={{ width: '100%' }}
            />
            <div style={{ fontSize: 11, color: '#666', marginTop: 4 }}>
              估算的往返延迟补偿量。网络差时增大，低延迟时减小。
            </div>
          </div>
          <div>
            <div className="label" style={{ marginBottom: 6, fontSize: 12, color: '#999' }}>
              回滚统计
            </div>
            <div style={{ fontFamily: 'monospace', fontSize: 13, lineHeight: 1.8 }}>
              <div>回滚次数: <span style={{ color: rollback.rollbackStats.rollbackCount > 0 ? '#ff6b6b' : '#88ff88' }}>
                {rollback.rollbackStats.rollbackCount}
              </span></div>
              <div>平均校正: {rollback.rollbackStats.avgCorrectionDistance.toFixed(2)}px</div>
              <div>已校正帧: {rollback.rollbackStats.totalCorrectedFrames}</div>
            </div>
          </div>
          <div>
            <div className="label" style={{ marginBottom: 6, fontSize: 12, color: '#999' }}>
              帧同步状态
            </div>
            <div style={{ fontFamily: 'monospace', fontSize: 13, lineHeight: 1.8 }}>
              <div>权威帧: {rollback.authorityPos ? '✓' : '—'}</div>
              <div>预测帧: {rollback.predictedEntity ? '✓' : '—'}</div>
              <div>其他玩家: {rollback.otherPlayers.length}</div>
            </div>
          </div>
        </div>
      </div>

      {rollbackEnabled && connected && (
        <RollbackDebugger
          predicted={rollback.predictedEntity}
          authority={rollback.authorityPos}
          otherPlayers={rollback.otherPlayers}
          correctionEvents={rollback.correctionEvents}
          width={800}
          height={600}
        />
      )}

      <div className="panel">
        <h2 style={{ margin: 0, fontSize: 16 }}>已连接手柄 ({pads.length})</h2>
        <div className="grid" style={{ marginTop: 14 }}>
          {pads.map((p) => (
            <GamepadCard
              key={p.index}
              pad={p}
              layout={layoutFor(p.index)}
              onLayoutChange={(l) => setLayout(p.index, l)}
            />
          ))}
          {pads.length === 0 && (
            <div style={{ color: '#888', fontSize: 13 }}>
              未检测到手柄。请连接 USB / 蓝牙手柄，然后按任意按钮以激活浏览器 Gamepad API。
            </div>
          )}
        </div>
      </div>

      <div className="panel">
        <h2 style={{ margin: 0, fontSize: 16 }}>日志</h2>
        <div className="log" style={{ marginTop: 10 }}>
          {log.map((l, i) => <div key={i}>{l}</div>)}
          {log.length === 0 && <div style={{ color: '#555' }}>（暂无日志）</div>}
        </div>
      </div>
    </div>
  );
}

function detectLayout(id: string): string {
  const low = id.toLowerCase();
  if (low.includes('054c') || low.includes('dualsense') || low.includes('dualshock')) return 'ps5';
  if (low.includes('057e') || low.includes('switch') || low.includes('pro controller')) return 'switch';
  return 'xbox';
}
