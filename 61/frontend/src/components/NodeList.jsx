import React from 'react';
import { shortId, fmtMs } from '../utils/utils.js';

export default function NodeList({ peers, chunkStates }) {
  return (
    <ul className="node-list">
      {peers.length === 0 && <div className="meta">No peers connected.</div>}
      {peers.map((p) => {
        const activeChunk = p.currentChunkId;
        const chunkInfo = activeChunk && chunkStates ? chunkStates[activeChunk] : null;
        const cls = p.status === 'busy' ? 'busy' : p.channelState === 'open' ? 'done' : 'idle';
        return (
          <li key={p.id} className={`node-item ${cls}`}>
            <div style={{ flex: 1 }}>
              <div style={{ fontWeight: 600 }}>
                {p.name || shortId(p.id)}{' '}
                <span className="badge worker">worker</span>
              </div>
              <div className="meta">
                {p.channelState} · {p.state}
                {activeChunk && (
                  <> · chunk {shortId(activeChunk)}</>
                )}
                {chunkInfo?.retryCount > 0 && (
                  <> · retry ×{chunkInfo.retryCount}</>
                )}
                {p.startedAt && (
                  <> · {fmtMs(Date.now() - p.startedAt)}</>
                )}
                {chunkInfo?.error && <div style={{ color: '#ff7b72' }}>{chunkInfo.error}</div>}
              </div>
            </div>
            <div className="status-dot" />
          </li>
        );
      })}
    </ul>
  );
}
